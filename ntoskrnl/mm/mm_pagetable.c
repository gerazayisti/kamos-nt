/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Four-level page tables: map, unmap, protect, translate. Owner: A04.
 *
 * Invariant I (kept by every operation, checked by the host tests): no page-table page below the
 * PML4 is empty, except the PDPTs of the kernel half, which exist for the life of the system.
 * It is what makes the rollback of a failed map exact without a log.
 */
#include "../../arch/x86_64/mm/pte.h"
#include "mm_internal.h"

typedef struct _MI_RANGE64 {
    ULONG_PTR First;
    ULONG_PTR Last;
} MI_RANGE64;

static const MI_RANGE64 MiKernelRegions[] = {
    {MI_DIRECT_MAP_BASE, MI_DIRECT_MAP_LAST}, {MI_POOL_BASE, MI_POOL_LAST},
    {MI_STACK_BASE, MI_STACK_LAST},           {MI_DEVICE_BASE, MI_DEVICE_LAST},
    {MI_IMAGE_BASE, MI_IMAGE_LAST},
};

static uint64_t *MiTable(const MM_CONTEXT *Mm, PHYSICAL_ADDRESS Physical)
{
    return Mm->Hw->PhysicalToPointer(Mm->Hw->Context, Physical, PAGE_SIZE);
}

/* Physical address of the table an entry points to, after sanity checks. */
static PHYSICAL_ADDRESS MiEntryTable(const MM_CONTEXT *Mm, uint64_t Entry)
{
    PHYSICAL_ADDRESS pa = Entry & MI_PTE_ADDR_MASK;
    if (pa >> Mm->MaxPhysicalBits != 0)
        MI_STOP(MM_STOP_TABLE_CORRUPT, Entry, 0, 0);
    return pa;
}

static uint64_t MiLeafBits(ULONG Prot)
{
    uint64_t v = MI_PTE_PRESENT;
    if (Prot & MM_PROT_WRITE)
        v |= MI_PTE_WRITE;
    if (Prot & MM_PROT_USER)
        v |= MI_PTE_USER;
    if (!(Prot & MM_PROT_EXECUTE))
        v |= MI_PTE_NX;
    return v;
}

/*
 * Checks alignment, canonical form, range and the user/kernel rule. Prot is checked only when
 * CheckProt is set. Sets *Kernel to true for a kernel-half range.
 */
static NTSTATUS MiCheckRange(ULONG_PTR Virtual, SIZE_T Count, ULONG Prot, BOOLEAN CheckProt,
                             BOOLEAN *Kernel)
{
    if ((Virtual & (PAGE_SIZE - 1u)) != 0 || !MI_VA_IS_CANONICAL(Virtual))
        return STATUS_INVALID_PARAMETER;
    if (Count == 0 || Count > (UINT64_C(1) << 36))
        return STATUS_INVALID_PARAMETER;
    ULONG_PTR last = Virtual + ((ULONG_PTR)Count << PAGE_SHIFT) - 1u;
    if (last < Virtual)
        return STATUS_INVALID_PARAMETER;

    BOOLEAN kernel = false;
    if (Virtual <= MI_USER_LAST) {
        if (Virtual < MI_USER_FIRST || last > MI_USER_LAST)
            return STATUS_INVALID_PARAMETER;
    } else if (Virtual >= MI_KERNEL_HALF_FIRST) {
        BOOLEAN inside = false;
        for (unsigned i = 0; i < KAMOS_ARRAY_SIZE(MiKernelRegions); i++)
            if (Virtual >= MiKernelRegions[i].First && last <= MiKernelRegions[i].Last)
                inside = true;
        if (!inside)
            return STATUS_INVALID_PARAMETER;
        kernel = true;
    } else {
        return STATUS_INVALID_PARAMETER; /* Top guard, or the canonical hole. */
    }
    if (CheckProt) {
        if ((Prot & ~MI_PROT_ALL) != 0 || !(Prot & MM_PROT_READ))
            return STATUS_INVALID_PARAMETER;
        if ((Prot & MM_PROT_WRITE) && (Prot & MM_PROT_EXECUTE))
            return STATUS_INVALID_PARAMETER;
        if (((Prot & MM_PROT_USER) != 0) == kernel)
            return STATUS_INVALID_PARAMETER; /* Kernel/user mismatch. */
    }
    *Kernel = kernel;
    return STATUS_SUCCESS;
}

typedef enum _MI_WALK {
    MiWalkLeaf,    /* *Leaf points to the PTE (present or not). */
    MiWalkMissing, /* A table on the way does not exist. */
    MiWalkLarge,   /* A large page covers the address. */
} MI_WALK;

static MI_WALK MiWalk(const MM_CONTEXT *Mm, PHYSICAL_ADDRESS Root, ULONG_PTR Virtual,
                      uint64_t **Leaf)
{
    uint64_t *table = MiTable(Mm, Root);
    for (unsigned level = 4; level >= 2; level--) {
        uint64_t e = table[MI_VA_INDEX(Virtual, level)];
        if (!(e & MI_PTE_PRESENT))
            return MiWalkMissing;
        if (e & MI_PTE_LARGE) {
            if (level == 4)
                MI_STOP(MM_STOP_TABLE_CORRUPT, e, Virtual, 4);
            return MiWalkLarge;
        }
        table = MiTable(Mm, MiEntryTable(Mm, e));
    }
    *Leaf = &table[MI_VA_INDEX(Virtual, 1)];
    return MiWalkLeaf;
}

/* Walks down, creating the missing tables. Fails only with STATUS_NO_MEMORY. */
static NTSTATUS MiEnsureLeaf(MM_CONTEXT *Mm, PHYSICAL_ADDRESS Root, ULONG_PTR Virtual, BOOLEAN User,
                             uint64_t **Leaf)
{
    uint64_t *table = MiTable(Mm, Root);
    for (unsigned level = 4; level >= 2; level--) {
        uint64_t *e = &table[MI_VA_INDEX(Virtual, level)];
        if (!(*e & MI_PTE_PRESENT)) {
            PHYSICAL_ADDRESS pa;
            NTSTATUS         st = Mm->TableFrames.Allocate(Mm->TableFrames.Context, &pa);
            if (!NT_SUCCESS(st))
                return st;
            *e = (pa & MI_PTE_ADDR_MASK) | MI_PTE_PRESENT | MI_PTE_WRITE | (User ? MI_PTE_USER : 0);
        } else if (*e & MI_PTE_LARGE) {
            return STATUS_OBJECT_NAME_COLLISION;
        }
        table = MiTable(Mm, MiEntryTable(Mm, *e));
    }
    *Leaf = &table[MI_VA_INDEX(Virtual, 1)];
    return STATUS_SUCCESS;
}

static BOOLEAN MiTableEmpty(const uint64_t *Table)
{
    for (unsigned i = 0; i < MI_ENTRIES_PER_TABLE; i++)
        if (Table[i] != 0)
            return false;
    return true;
}

/* Frees the empty tables on the path of Virtual, bottom up. */
static void MiPrune(MM_CONTEXT *Mm, PHYSICAL_ADDRESS Root, ULONG_PTR Virtual, BOOLEAN Kernel)
{
    uint64_t        *tbl[5] = {NULL};
    uint64_t        *ent[5] = {NULL};
    PHYSICAL_ADDRESS tpa[5] = {0};

    tbl[4]          = MiTable(Mm, Root);
    unsigned lowest = 4;
    for (unsigned level = 4; level >= 2; level--) {
        ent[level] = &tbl[level][MI_VA_INDEX(Virtual, level)];
        if (!(*ent[level] & MI_PTE_PRESENT) || (*ent[level] & MI_PTE_LARGE))
            break;
        tpa[level - 1u] = MiEntryTable(Mm, *ent[level]);
        tbl[level - 1u] = MiTable(Mm, tpa[level - 1u]);
        lowest          = level - 1u;
    }
    for (unsigned level = lowest; level <= 3; level++) {
        if (level == 3 && Kernel)
            break; /* Kernel-half PDPTs are permanent. */
        if (!MiTableEmpty(tbl[level]))
            break;
        *ent[level + 1u] = 0;
        Mm->TableFrames.Free(Mm->TableFrames.Context, tpa[level]);
    }
}

static void MiPruneRange(MM_CONTEXT *Mm, PHYSICAL_ADDRESS Root, ULONG_PTR Virtual, SIZE_T Count,
                         BOOLEAN Kernel)
{
    ULONG_PTR last = Virtual + (((ULONG_PTR)Count << PAGE_SHIFT) - 1u); /* Validated: no wrap. */
    ULONG_PTR va   = Virtual;
    for (;;) {
        MiPrune(Mm, Root, va, Kernel);
        ULONG_PTR next = (va | UINT64_C(0x1FFFFF)) + 1u; /* Next 2 MiB block; 0 at the top. */
        if (next == 0 || next > last)
            break;
        va = next;
    }
}

static void MiLockSpace(ADDRESS_SPACE *Space, BOOLEAN Kernel)
{
    ADDRESS_SPACE *l = Kernel ? &Space->Mm->KernelSpace : Space;
    Space->Mm->Hw->AcquireLock(Space->Mm->Hw->Context, LOCK_RANK_ADDRESS_SPACE, &l->Lock);
}

static void MiUnlockSpace(ADDRESS_SPACE *Space, BOOLEAN Kernel)
{
    ADDRESS_SPACE *l = Kernel ? &Space->Mm->KernelSpace : Space;
    Space->Mm->Hw->ReleaseLock(Space->Mm->Hw->Context, LOCK_RANK_ADDRESS_SPACE, &l->Lock);
}

static void MiCheckSpace(const ADDRESS_SPACE *Space)
{
    if (Space == NULL || Space->Magic != MI_SPACE_MAGIC || Space->Mm == NULL)
        MI_STOP(MM_STOP_SPACE_BAD, (ULONG_PTR)Space, 0, 0);
}

static void MiInvalidate(const ADDRESS_SPACE *Space, BOOLEAN Kernel, ULONG_PTR Virtual)
{
    const MM_HW_OPS *hw = Space->Mm->Hw;
    if (Kernel || hw->GetCurrentRoot(hw->Context) == Space->Root)
        hw->InvalidatePage(hw->Context, Virtual);
}

NTSTATUS MiMapPages(ADDRESS_SPACE *Space, ULONG_PTR Virtual, PHYSICAL_ADDRESS Physical,
                    SIZE_T Count, ULONG Prot)
{
    MiCheckSpace(Space);
    MM_CONTEXT *mm = Space->Mm;
    BOOLEAN     kernel;
    NTSTATUS    st = MiCheckRange(Virtual, Count, Prot, true, &kernel);
    if (!NT_SUCCESS(st))
        return st;
    if ((Physical & (PAGE_SIZE - 1u)) != 0)
        return STATUS_INVALID_PARAMETER;
    PHYSICAL_ADDRESS bytes    = (PHYSICAL_ADDRESS)Count << PAGE_SHIFT;
    PHYSICAL_ADDRESS lastPhys = Physical + bytes - 1u;
    if (lastPhys < Physical || (lastPhys >> mm->MaxPhysicalBits) != 0)
        return STATUS_INVALID_PARAMETER;

    /* User pages may only be frames the caller allocated: never a page table, a free frame or
     * kernel memory. */
    if (Prot & MM_PROT_USER) {
        for (SIZE_T i = 0; i < Count; i++) {
            const MM_PFN *p =
                MiGetFrame(mm->Frames, Physical + ((PHYSICAL_ADDRESS)i << PAGE_SHIFT));
            if (p == NULL || p->State != MmFrameActive)
                return STATUS_INVALID_PARAMETER;
        }
    }

    MiLockSpace(Space, kernel);
    /* Phase 1: nothing is written until every page is known to be free. */
    for (SIZE_T i = 0; i < Count; i++) {
        uint64_t *leaf = NULL;
        MI_WALK   w    = MiWalk(mm, Space->Root, Virtual + ((ULONG_PTR)i << PAGE_SHIFT), &leaf);
        if (w == MiWalkLarge || (w == MiWalkLeaf && (*leaf & MI_PTE_PRESENT))) {
            MiUnlockSpace(Space, kernel);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }
    /* Phase 2. */
    uint64_t bits = MiLeafBits(Prot);
    SIZE_T   done = 0;
    for (; done < Count; done++) {
        uint64_t *leaf = NULL;
        ULONG_PTR va   = Virtual + ((ULONG_PTR)done << PAGE_SHIFT);
        st             = MiEnsureLeaf(mm, Space->Root, va, !kernel, &leaf);
        if (!NT_SUCCESS(st))
            break;
        *leaf = ((Physical + ((PHYSICAL_ADDRESS)done << PAGE_SHIFT)) & MI_PTE_ADDR_MASK) | bits;
    }
    if (done < Count) {
        /* Roll back: clear what was written, then free the tables that became empty. */
        for (SIZE_T i = 0; i < done; i++) {
            uint64_t *leaf = NULL;
            if (MiWalk(mm, Space->Root, Virtual + ((ULONG_PTR)i << PAGE_SHIFT), &leaf) !=
                MiWalkLeaf)
                MI_STOP(MM_STOP_TABLE_CORRUPT, Virtual, i, 0);
            *leaf = 0;
        }
        MiPruneRange(mm, Space->Root, Virtual, Count, kernel);
        MiUnlockSpace(Space, kernel);
        return st;
    }
    MiUnlockSpace(Space, kernel);
    return STATUS_SUCCESS;
}

NTSTATUS MiUnmapPages(ADDRESS_SPACE *Space, ULONG_PTR Virtual, SIZE_T Count)
{
    MiCheckSpace(Space);
    MM_CONTEXT *mm = Space->Mm;
    BOOLEAN     kernel;
    NTSTATUS    st = MiCheckRange(Virtual, Count, 0, false, &kernel);
    if (!NT_SUCCESS(st))
        return st;

    MiLockSpace(Space, kernel);
    for (SIZE_T i = 0; i < Count; i++) {
        uint64_t *leaf = NULL;
        MI_WALK   w    = MiWalk(mm, Space->Root, Virtual + ((ULONG_PTR)i << PAGE_SHIFT), &leaf);
        if (w == MiWalkLarge) {
            MiUnlockSpace(Space, kernel);
            return STATUS_NOT_IMPLEMENTED;
        }
        if (w == MiWalkMissing || !(*leaf & MI_PTE_PRESENT)) {
            MiUnlockSpace(Space, kernel);
            return STATUS_INVALID_PARAMETER;
        }
    }
    for (SIZE_T i = 0; i < Count; i++) {
        uint64_t *leaf = NULL;
        ULONG_PTR va   = Virtual + ((ULONG_PTR)i << PAGE_SHIFT);
        (void)MiWalk(mm, Space->Root, va, &leaf);
        *leaf = 0;
        MiInvalidate(Space, kernel, va);
    }
    MiPruneRange(mm, Space->Root, Virtual, Count, kernel);
    MiUnlockSpace(Space, kernel);
    return STATUS_SUCCESS;
}

NTSTATUS MiProtectPages(ADDRESS_SPACE *Space, ULONG_PTR Virtual, SIZE_T Count, ULONG Prot)
{
    MiCheckSpace(Space);
    MM_CONTEXT *mm = Space->Mm;
    BOOLEAN     kernel;
    NTSTATUS    st = MiCheckRange(Virtual, Count, Prot, true, &kernel);
    if (!NT_SUCCESS(st))
        return st;

    MiLockSpace(Space, kernel);
    for (SIZE_T i = 0; i < Count; i++) {
        uint64_t *leaf = NULL;
        MI_WALK   w    = MiWalk(mm, Space->Root, Virtual + ((ULONG_PTR)i << PAGE_SHIFT), &leaf);
        if (w == MiWalkLarge) {
            MiUnlockSpace(Space, kernel);
            return STATUS_NOT_IMPLEMENTED;
        }
        if (w == MiWalkMissing || !(*leaf & MI_PTE_PRESENT)) {
            MiUnlockSpace(Space, kernel);
            return STATUS_INVALID_PARAMETER;
        }
    }
    uint64_t bits = MiLeafBits(Prot);
    for (SIZE_T i = 0; i < Count; i++) {
        uint64_t *leaf = NULL;
        ULONG_PTR va   = Virtual + ((ULONG_PTR)i << PAGE_SHIFT);
        (void)MiWalk(mm, Space->Root, va, &leaf);
        /* Keep the address and the bits the processor maintains (accessed, dirty). */
        *leaf = (*leaf & (MI_PTE_ADDR_MASK | MI_PTE_ACCESSED | MI_PTE_DIRTY)) | bits;
        MiInvalidate(Space, kernel, va);
    }
    MiUnlockSpace(Space, kernel);
    return STATUS_SUCCESS;
}

NTSTATUS MiTranslate(const ADDRESS_SPACE *Space, ULONG_PTR Virtual, PHYSICAL_ADDRESS *Physical,
                     ULONG *Prot)
{
    MiCheckSpace(Space);
    const MM_CONTEXT *mm = Space->Mm;
    if (!MI_VA_IS_CANONICAL(Virtual))
        return STATUS_INVALID_PARAMETER;

    BOOLEAN          write = true;
    BOOLEAN          user  = true;
    BOOLEAN          nx    = false;
    PHYSICAL_ADDRESS pa    = 0;
    uint64_t        *table = MiTable(mm, Space->Root);
    for (unsigned level = 4; level >= 1; level--) {
        uint64_t e = table[MI_VA_INDEX(Virtual, level)];
        if (!(e & MI_PTE_PRESENT))
            return STATUS_OBJECT_NAME_NOT_FOUND;
        write = write && (e & MI_PTE_WRITE);
        user  = user && (e & MI_PTE_USER);
        nx    = nx || (e & MI_PTE_NX);
        if (level == 4 && (e & MI_PTE_LARGE))
            MI_STOP(MM_STOP_TABLE_CORRUPT, e, Virtual, 4);
        if (level == 3 && (e & MI_PTE_LARGE)) {
            pa = (e & MI_PTE_LARGE1G_ADDR_MASK) | (Virtual & (UINT64_C(0x3FFFFFFF)));
            break;
        }
        if (level == 2 && (e & MI_PTE_LARGE)) {
            pa = (e & MI_PTE_LARGE2M_ADDR_MASK) | (Virtual & (UINT64_C(0x1FFFFF)));
            break;
        }
        if (level == 1) {
            pa = (e & MI_PTE_ADDR_MASK) | (Virtual & (PAGE_SIZE - 1u));
            break;
        }
        table = MiTable(mm, MiEntryTable(mm, e));
    }
    if (Physical != NULL)
        *Physical = pa;
    if (Prot != NULL)
        *Prot = MM_PROT_READ | (write ? MM_PROT_WRITE : 0) | (user ? MM_PROT_USER : 0) |
                (nx ? 0 : MM_PROT_EXECUTE);
    return STATUS_SUCCESS;
}

NTSTATUS MiBuildDirectMap(ADDRESS_SPACE *Kernel, const BOOT_INFORMATION *Boot)
{
    for (ULONG i = 0; i < Boot->RegionCount; i++) {
        const BOOT_MEMORY_REGION *r = &Boot->Regions[i];
        if (r->Length == 0 || (r->Type != BootMemoryUsable && r->Type != BootMemoryAcpiReclaimable))
            continue;
        ULONG64 first = (r->Base + (PAGE_SIZE - 1u)) >> PAGE_SHIFT;
        ULONG64 end   = (r->Base + r->Length) >> PAGE_SHIFT;
        ULONG64 limit = (MI_DIRECT_MAP_LAST - MI_DIRECT_MAP_BASE + 1u) >> PAGE_SHIFT;
        if (end > limit)
            end = limit;
        if (first >= end)
            continue;
        NTSTATUS st = MiMapPages(Kernel, MI_DIRECT_MAP_BASE + (first << PAGE_SHIFT),
                                 first << PAGE_SHIFT, end - first, MM_PROT_READ | MM_PROT_WRITE);
        if (!NT_SUCCESS(st))
            return st;
    }
    return STATUS_SUCCESS;
}

static ULONG64 MiCountSubtree(const MM_CONTEXT *Mm, PHYSICAL_ADDRESS Table, unsigned Level)
{
    ULONG64         n = 1;
    const uint64_t *t = MiTable(Mm, Table);
    if (Level == 1)
        return n;
    for (unsigned i = 0; i < MI_ENTRIES_PER_TABLE; i++) {
        if ((t[i] & MI_PTE_PRESENT) && !(t[i] & MI_PTE_LARGE))
            n += MiCountSubtree(Mm, MiEntryTable(Mm, t[i]), Level - 1u);
    }
    return n;
}

ULONG64 MiCountTables(const MM_CONTEXT *Mm, PHYSICAL_ADDRESS Root, BOOLEAN KernelHalfToo)
{
    ULONG64         n = 1;
    const uint64_t *t = MiTable(Mm, Root);
    for (unsigned i = 0; i < (KernelHalfToo ? 512u : 256u); i++)
        if ((t[i] & MI_PTE_PRESENT) && !(t[i] & MI_PTE_LARGE))
            n += MiCountSubtree(Mm, MiEntryTable(Mm, t[i]), 3);
    return n;
}

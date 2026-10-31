/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Address spaces. Owner: A04.
 *
 * The kernel half (PML4 entries 256 to 511) is built once, with a PDPT behind every entry, and
 * never changes afterwards. New spaces copy it without taking a lock. Everything below the PDPTs
 * of the kernel half is shared and protected by the kernel space lock (rank 30).
 */
#include "../../arch/x86_64/mm/pte.h"
#include "mm_internal.h"

#define MI_KERNEL_PML4_FIRST 256u

void *memset(void *Dest, int Value, size_t Count);
void *memcpy(void *Dest, const void *Source, size_t Count);

static NTSTATUS MiDefaultTableAllocate(void *Context, PHYSICAL_ADDRESS *Physical)
{
    MM_CONTEXT *mm = Context;
    return MiAllocateFrame(mm->Frames, MmFramePageTable, true, Physical);
}

static void MiDefaultTableFree(void *Context, PHYSICAL_ADDRESS Physical)
{
    MM_CONTEXT *mm = Context;
    (void)MiDereferenceFrame(mm->Frames, Physical, MmFramePageTable);
}

void MiContextInit(MM_CONTEXT *Mm, const MM_HW_OPS *Hw, MM_FRAME_DB *Frames, ULONG MaxPhysicalBits)
{
    memset(Mm, 0, sizeof *Mm);
    Mm->Hw                   = Hw;
    Mm->Frames               = Frames;
    Mm->MaxPhysicalBits      = MaxPhysicalBits;
    Mm->TableFrames.Context  = Mm;
    Mm->TableFrames.Allocate = MiDefaultTableAllocate;
    Mm->TableFrames.Free     = MiDefaultTableFree;
}

NTSTATUS MiInitKernelSpace(MM_CONTEXT *Mm, PHYSICAL_ADDRESS ExistingRoot)
{
    PHYSICAL_ADDRESS root = ExistingRoot;
    NTSTATUS         st;
    if (root == 0) {
        st = Mm->TableFrames.Allocate(Mm->TableFrames.Context, &root);
        if (!NT_SUCCESS(st))
            return st;
    }
    uint64_t *pml4       = Mm->Hw->PhysicalToPointer(Mm->Hw->Context, root, PAGE_SIZE);
    UCHAR     added[256] = {0};
    st                   = STATUS_SUCCESS;
    for (unsigned i = MI_KERNEL_PML4_FIRST; i < 512u; i++) {
        if (pml4[i] & MI_PTE_PRESENT)
            continue;
        PHYSICAL_ADDRESS pdpt;
        st = Mm->TableFrames.Allocate(Mm->TableFrames.Context, &pdpt);
        if (!NT_SUCCESS(st))
            break;
        pml4[i]                         = (pdpt & MI_PTE_ADDR_MASK) | MI_PTE_PRESENT | MI_PTE_WRITE;
        added[i - MI_KERNEL_PML4_FIRST] = 1;
    }
    if (!NT_SUCCESS(st)) {
        for (unsigned i = MI_KERNEL_PML4_FIRST; i < 512u; i++) {
            if (added[i - MI_KERNEL_PML4_FIRST]) {
                Mm->TableFrames.Free(Mm->TableFrames.Context, pml4[i] & MI_PTE_ADDR_MASK);
                pml4[i] = 0;
            }
        }
        if (ExistingRoot == 0)
            Mm->TableFrames.Free(Mm->TableFrames.Context, root);
        return st;
    }
    Mm->KernelSpace.Magic    = MI_SPACE_MAGIC;
    Mm->KernelSpace.IsKernel = true;
    Mm->KernelSpace.Root     = root;
    Mm->KernelSpace.Mm       = Mm;
    return STATUS_SUCCESS;
}

NTSTATUS MiCreateAddressSpace(MM_CONTEXT *Mm, ADDRESS_SPACE **Space)
{
    if (Space == NULL)
        return STATUS_INVALID_PARAMETER;
    *Space           = NULL;
    ADDRESS_SPACE *s = MiPoolAllocate(&Mm->Pool, NonPagedPool, sizeof *s, 0x70536441u /* 'AdSp' */);
    if (s == NULL)
        return STATUS_NO_MEMORY;
    memset(s, 0, sizeof *s);
    NTSTATUS st = Mm->TableFrames.Allocate(Mm->TableFrames.Context, &s->Root);
    if (!NT_SUCCESS(st)) {
        MiPoolFree(&Mm->Pool, s, 0x70536441u);
        return st;
    }
    uint64_t       *dst = Mm->Hw->PhysicalToPointer(Mm->Hw->Context, s->Root, PAGE_SIZE);
    const uint64_t *src =
        Mm->Hw->PhysicalToPointer(Mm->Hw->Context, Mm->KernelSpace.Root, PAGE_SIZE);
    memcpy(&dst[MI_KERNEL_PML4_FIRST], &src[MI_KERNEL_PML4_FIRST],
           (512u - MI_KERNEL_PML4_FIRST) * sizeof(uint64_t));
    s->Magic    = MI_SPACE_MAGIC;
    s->IsKernel = false;
    s->Mm       = Mm;
    *Space      = s;
    return STATUS_SUCCESS;
}

/* Frees a table and everything below it. Leaf frames belong to whoever mapped them. */
static void MiFreeSubtree(MM_CONTEXT *Mm, PHYSICAL_ADDRESS Table, unsigned Level)
{
    if (Level > 1) {
        uint64_t *t = Mm->Hw->PhysicalToPointer(Mm->Hw->Context, Table, PAGE_SIZE);
        for (unsigned i = 0; i < MI_ENTRIES_PER_TABLE; i++)
            if ((t[i] & MI_PTE_PRESENT) && !(t[i] & MI_PTE_LARGE))
                MiFreeSubtree(Mm, t[i] & MI_PTE_ADDR_MASK, Level - 1u);
    }
    Mm->TableFrames.Free(Mm->TableFrames.Context, Table);
}

void MiDestroyAddressSpace(ADDRESS_SPACE *Space)
{
    if (Space == NULL || Space->Magic != MI_SPACE_MAGIC || Space->Mm == NULL) {
        MI_STOP(MM_STOP_SPACE_BAD, (ULONG_PTR)Space, 0, 0);
        return;
    }
    MM_CONTEXT *mm = Space->Mm;
    if (Space->IsKernel || Space == &mm->KernelSpace)
        MI_STOP(MM_STOP_SPACE_DESTROY_KERNEL, (ULONG_PTR)Space, 0, 0);
    if (Space == mm->Current || mm->Hw->GetCurrentRoot(mm->Hw->Context) == Space->Root)
        MI_STOP(MM_STOP_SPACE_DESTROY_CURRENT, (ULONG_PTR)Space, Space->Root, 0);

    uint64_t *pml4 = mm->Hw->PhysicalToPointer(mm->Hw->Context, Space->Root, PAGE_SIZE);
    for (unsigned i = 0; i < MI_KERNEL_PML4_FIRST; i++)
        if ((pml4[i] & MI_PTE_PRESENT) && !(pml4[i] & MI_PTE_LARGE))
            MiFreeSubtree(mm, pml4[i] & MI_PTE_ADDR_MASK, 3);
    mm->TableFrames.Free(mm->TableFrames.Context, Space->Root);
    Space->Magic = 0;
    MiPoolFree(&mm->Pool, Space, 0x70536441u);
}

void MiSwitchAddressSpace(ADDRESS_SPACE *Space)
{
    if (Space == NULL || Space->Magic != MI_SPACE_MAGIC || Space->Mm == NULL) {
        MI_STOP(MM_STOP_SPACE_BAD, (ULONG_PTR)Space, 0, 0);
        return;
    }
    Space->Mm->Hw->SetCurrentRoot(Space->Mm->Hw->Context, Space->Root);
    Space->Mm->Current = Space;
}

BOOLEAN MiIsUserRange(ULONG_PTR Address, SIZE_T Length)
{
    if (Length == 0)
        return true; /* Nothing is touched. */
    if (Address < MI_USER_FIRST || Address > MI_USER_LAST)
        return false;
    return (ULONG_PTR)(Length - 1u) <= MI_USER_LAST - Address;
}

BOOLEAN MiIsDirectMap(ULONG_PTR Address)
{
    return Address >= MI_DIRECT_MAP_BASE && Address <= MI_DIRECT_MAP_LAST;
}

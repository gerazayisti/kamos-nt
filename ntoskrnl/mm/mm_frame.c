/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Boot allocator and frame database. Owner: A04. See mm_frame.h.
 */
#include "mm_frame.h"

#include <kamos/ke.h>

void *memset(void *Dest, int Value, size_t Count);

#define MI_PHYS_LIMIT (UINT64_C(1) << MM_MAX_PHYSICAL_BITS)

static ULONG64 MiCeilPfn(ULONG64 Address)
{
    return (Address + (PAGE_SIZE - 1u)) >> PAGE_SHIFT;
}

static ULONG64 MiFloorPfn(ULONG64 Address)
{
    return Address >> PAGE_SHIFT;
}

/* Higher rank wins when two sources describe the same frame. */
static unsigned MiRank(UCHAR State)
{
    switch (State) {
    case MmFrameNonexistent:
        return 0;
    case MmFrameFree:
        return 1;
    case MmFrameAcpi:
        return 2;
    case MmFrameReserved:
        return 3;
    case MmFrameBad:
        return 4;
    default:
        return 5;
    }
}

static void MiAddReservation(MM_FRAME_DB *Db, ULONG64 Base, ULONG64 Length)
{
    if (Length == 0)
        return;
    if (Base + Length < Base || Base + Length > MI_PHYS_LIMIT)
        MI_BOOT_STOP(MM_BOOT_BAD_RESERVATION, Base, Length, 0);
    if (Db->ReservationCount >= MM_MAX_RESERVATIONS)
        MI_BOOT_STOP(MM_BOOT_BAD_RESERVATION, Base, Length, Db->ReservationCount);
    Db->Reservations[Db->ReservationCount].First = MiFloorPfn(Base);
    Db->Reservations[Db->ReservationCount].End   = MiCeilPfn(Base + Length);
    Db->ReservationCount++;
}

/* Removes [First, End) from every boot range, splitting where needed. */
static void MiSubtractRange(MM_FRAME_DB *Db, ULONG64 First, ULONG64 End)
{
    for (ULONG i = 0; i < Db->BootRangeCount; i++) {
        MM_PFN_RANGE *r = &Db->BootRanges[i];
        if (End <= r->First || First >= r->End)
            continue;
        if (First <= r->First && End >= r->End) {
            Db->BootRanges[i] = Db->BootRanges[Db->BootRangeCount - 1u];
            Db->BootRangeCount--;
            i--; /* Re-examine the entry moved into this slot (unsigned wrap is intended). */
        } else if (First <= r->First) {
            r->First = End;
        } else if (End >= r->End) {
            r->End = First;
        } else {
            if (Db->BootRangeCount >= MM_MAX_BOOT_RANGES)
                MI_BOOT_STOP(MM_BOOT_TOO_MANY_RANGES, First, End, 0);
            Db->BootRanges[Db->BootRangeCount].First = End;
            Db->BootRanges[Db->BootRangeCount].End   = r->End;
            Db->BootRangeCount++;
            r->End = First;
        }
    }
}

void MiBootAllocatorInit(MM_FRAME_DB *Db, const MM_HW_OPS *Hw, const BOOT_INFORMATION *Boot,
                         const MM_RESERVED_RANGE *Extra, ULONG ExtraCount)
{
    memset(Db, 0, sizeof *Db);
    Db->Hw   = Hw;
    Db->Boot = Boot;

    if (Boot == NULL) {
        MI_BOOT_STOP(MM_BOOT_BAD_REGION_COUNT, 0, 0, 0);
        return;
    }
    if (Boot->RegionCount == 0 || Boot->RegionCount > BOOT_MAX_MEMORY_REGIONS)
        MI_BOOT_STOP(MM_BOOT_BAD_REGION_COUNT, Boot->RegionCount, 0, 0);
    if (Boot->ModuleCount > BOOT_MAX_MODULES || ExtraCount > MM_MAX_EXTRA_RESERVATIONS ||
        (ExtraCount != 0 && Extra == NULL))
        MI_BOOT_STOP(MM_BOOT_BAD_RESERVATION, Boot->ModuleCount, ExtraCount, 0);

    ULONG64 prevBase         = 0;
    ULONG64 prevEnd          = 0;
    ULONG64 highestUsableEnd = 0;
    BOOLEAN seen             = false;
    for (ULONG i = 0; i < Boot->RegionCount; i++) {
        const BOOT_MEMORY_REGION *r = &Boot->Regions[i];
        if (r->Length == 0)
            continue; /* The HAL drops these; tolerated. */
        if ((int)r->Type < (int)BootMemoryUsable || (int)r->Type > (int)BootMemoryBad)
            MI_BOOT_STOP(MM_BOOT_BAD_REGION_TYPE, i, r->Type, 0);
        ULONG64 end = r->Base + r->Length;
        if (end < r->Base || end > MI_PHYS_LIMIT)
            MI_BOOT_STOP(MM_BOOT_REGION_OVERFLOW, i, r->Base, r->Length);
        if (seen) {
            if (r->Base < prevBase)
                MI_BOOT_STOP(MM_BOOT_NOT_SORTED, i, r->Base, prevBase);
            if (r->Base < prevEnd)
                MI_BOOT_STOP(MM_BOOT_OVERLAP, i, r->Base, prevEnd);
        }
        seen     = true;
        prevBase = r->Base;
        prevEnd  = end;
        if (r->Type == BootMemoryUsable) {
            ULONG64 first = MiCeilPfn(r->Base);
            ULONG64 last  = MiFloorPfn(end);
            if (first < last) {
                if (Db->BootRangeCount >= MM_MAX_BOOT_RANGES)
                    MI_BOOT_STOP(MM_BOOT_TOO_MANY_RANGES, i, 0, 0);
                Db->BootRanges[Db->BootRangeCount].First = first;
                Db->BootRanges[Db->BootRangeCount].End   = last;
                Db->BootRangeCount++;
                if (MiCeilPfn(end) > highestUsableEnd)
                    highestUsableEnd = MiCeilPfn(end);
            }
        }
    }
    if (Db->BootRangeCount == 0)
        MI_BOOT_STOP(MM_BOOT_NO_USABLE_MEMORY, 0, 0, 0);
    Db->FrameCount = highestUsableEnd;

    /* The first MiB is never handed out (legacy structures, AP start-up code). */
    MiAddReservation(Db, 0, (ULONG64)MM_FIRST_MIB_PAGES << PAGE_SHIFT);
    for (ULONG i = 0; i < Boot->ModuleCount; i++)
        MiAddReservation(Db, Boot->Modules[i].Base, Boot->Modules[i].Length);
    if (Boot->Framebuffer.Present)
        MiAddReservation(Db, Boot->Framebuffer.Base,
                         (ULONG64)Boot->Framebuffer.Pitch * Boot->Framebuffer.Height);
    if (Boot->AcpiRsdp != 0)
        MiAddReservation(Db, Boot->AcpiRsdp & ~(ULONG64)(PAGE_SIZE - 1u), PAGE_SIZE);
    for (ULONG i = 0; i < ExtraCount; i++)
        MiAddReservation(Db, Extra[i].Base, Extra[i].Length);

    for (ULONG i = 0; i < Db->ReservationCount; i++)
        MiSubtractRange(Db, Db->Reservations[i].First, Db->Reservations[i].End);
    if (Db->BootRangeCount == 0)
        MI_BOOT_STOP(MM_BOOT_NO_USABLE_MEMORY, 1, 0, 0);
}

NTSTATUS MiBootAllocatePages(MM_FRAME_DB *Db, ULONG64 Count, PHYSICAL_ADDRESS *Out)
{
    if (Db->BootClosed)
        MI_STOP(MM_STOP_BOOT_ALLOC_CLOSED, Count, 0, 0);
    if (Count == 0 || Out == NULL)
        return STATUS_INVALID_PARAMETER;

    MM_PFN_RANGE *best = NULL;
    for (ULONG i = 0; i < Db->BootRangeCount; i++) {
        MM_PFN_RANGE *r = &Db->BootRanges[i];
        if (r->End - r->First >= Count && (best == NULL || r->End > best->End))
            best = r;
    }
    if (best == NULL)
        return STATUS_NO_MEMORY;
    best->End -= Count;
    *Out = best->End << PAGE_SHIFT;
    if (best->First == best->End) {
        *best = Db->BootRanges[Db->BootRangeCount - 1u];
        Db->BootRangeCount--;
    }
    return STATUS_SUCCESS;
}

static void MiSetState(MM_FRAME_DB *Db, ULONG64 Index, UCHAR State, UCHAR Flags)
{
    Db->StateCount[Db->Pfn[Index].State]--;
    Db->Pfn[Index].State = State;
    Db->Pfn[Index].Flags = Flags;
    Db->StateCount[State]++;
}

static void MiMarkRange(MM_FRAME_DB *Db, ULONG64 First, ULONG64 End, UCHAR State, UCHAR Flags,
                        BOOLEAN Override)
{
    if (End > Db->FrameCount)
        End = Db->FrameCount;
    for (ULONG64 i = First; i < End; i++) {
        unsigned cur  = MiRank(Db->Pfn[i].State);
        unsigned want = MiRank(State);
        if (want > cur || (Override && want == cur && want <= MiRank(MmFrameReserved)))
            MiSetState(Db, i, State, Flags);
    }
}

void MiFrameDbInit(MM_FRAME_DB *Db)
{
    if (Db->Initialised || Db->BootClosed)
        MI_STOP(MM_STOP_BOOT_ALLOC_CLOSED, 1, 0, 0);
    const BOOT_INFORMATION *boot   = Db->Boot;
    ULONG64                 frames = Db->FrameCount;

    ULONG64          pfnBytes = frames * sizeof(MM_PFN);
    ULONG64          words    = (frames + 63u) / 64u;
    ULONG64          pages    = (pfnBytes + words * 8u + (PAGE_SIZE - 1u)) >> PAGE_SHIFT;
    PHYSICAL_ADDRESS where;
    if (MiBootAllocatePages(Db, pages, &where) != STATUS_SUCCESS)
        MI_BOOT_STOP(MM_BOOT_DB_NO_ROOM, frames, pages, 0);
    Db->DbPhysical  = where;
    Db->DbPages     = pages;
    Db->BitmapWords = words;
    Db->Pfn         = Db->Hw->PhysicalToPointer(Db->Hw->Context, where, pages << PAGE_SHIFT);
    Db->Bitmap      = (ULONG64 *)(void *)((UCHAR *)Db->Pfn + pfnBytes);
    memset(Db->Pfn, 0, pages << PAGE_SHIFT);
    Db->StateCount[MmFrameNonexistent] = frames;

    /* Every usable page starts as "taken by early boot" and is released below if still free. */
    for (ULONG i = 0; i < boot->RegionCount; i++) {
        const BOOT_MEMORY_REGION *r = &boot->Regions[i];
        if (r->Length == 0 || r->Type != BootMemoryUsable)
            continue;
        MiMarkRange(Db, MiCeilPfn(r->Base), MiFloorPfn(r->Base + r->Length), MmFrameReserved,
                    MM_PFN_FLAG_BOOT, false);
    }
    for (ULONG i = 0; i < Db->BootRangeCount; i++) {
        ULONG64 end = Db->BootRanges[i].End;
        for (ULONG64 f = Db->BootRanges[i].First; f < end; f++)
            MiSetState(Db, f, MmFrameFree, 0);
    }
    /* Non-usable regions grow outward to whole pages and win over usable memory. */
    for (ULONG i = 0; i < boot->RegionCount; i++) {
        const BOOT_MEMORY_REGION *r = &boot->Regions[i];
        if (r->Length == 0 || r->Type == BootMemoryUsable)
            continue;
        UCHAR state = MmFrameReserved;
        if (r->Type == BootMemoryAcpiReclaimable)
            state = MmFrameAcpi;
        else if (r->Type == BootMemoryBad)
            state = MmFrameBad;
        MiMarkRange(Db, MiFloorPfn(r->Base), MiCeilPfn(r->Base + r->Length), state, 0, false);
    }
    for (ULONG i = 0; i < Db->ReservationCount; i++)
        MiMarkRange(Db, Db->Reservations[i].First, Db->Reservations[i].End, MmFrameReserved, 0,
                    true);
    for (ULONG64 f = where >> PAGE_SHIFT; f < (where >> PAGE_SHIFT) + pages; f++)
        MiSetState(Db, f, MmFrameReserved, MM_PFN_FLAG_BOOT | MM_PFN_FLAG_DATABASE);

    for (ULONG64 f = 0; f < frames; f++) {
        if (Db->Pfn[f].State == MmFrameFree) {
            Db->Bitmap[f / 64u] |= UINT64_C(1) << (f % 64u);
            if (Db->Hw->NotifyFrame != NULL)
                Db->Hw->NotifyFrame(Db->Hw->Context, f << PAGE_SHIFT, MmFrameFree);
        }
    }
    Db->Hint        = 0;
    Db->BootClosed  = true;
    Db->Initialised = true;
}

NTSTATUS MiAllocateFrame(MM_FRAME_DB *Db, MM_FRAME_STATE State, BOOLEAN Zero, PHYSICAL_ADDRESS *Out)
{
    if ((State != MmFrameActive && State != MmFramePageTable) || Out == NULL || !Db->Initialised)
        MI_STOP(MM_STOP_FRAME_BAD_STATE, State, 0, 0);

    const MM_HW_OPS *hw    = Db->Hw;
    ULONG64          index = 0;
    BOOLEAN          found = false;
    hw->AcquireLock(hw->Context, LOCK_RANK_FRAME_DB, &Db->Lock);
    if (Db->StateCount[MmFrameFree] != 0) {
        ULONG64 w = Db->Hint;
        for (ULONG64 n = 0; n < Db->BitmapWords; n++) {
            if (Db->Bitmap[w] != 0) {
                index = w * 64u + (ULONG64)__builtin_ctzll(Db->Bitmap[w]);
                Db->Bitmap[w] &= Db->Bitmap[w] - 1u; /* Clears the lowest set bit. */
                Db->Hint = w;
                found    = true;
                break;
            }
            w = (w + 1u == Db->BitmapWords) ? 0u : w + 1u;
        }
        if (found) {
            if (index >= Db->FrameCount)
                MI_STOP(MM_STOP_TABLE_CORRUPT, index, Db->FrameCount, 0);
            if (Db->Pfn[index].State != MmFrameFree)
                MI_STOP(MM_STOP_TABLE_CORRUPT, index, Db->Pfn[index].State, 1);
            MiSetState(Db, index, (UCHAR)State, 0);
            Db->Pfn[index].RefCount = 1;
        }
    }
    hw->ReleaseLock(hw->Context, LOCK_RANK_FRAME_DB, &Db->Lock);
    if (!found)
        return STATUS_NO_MEMORY;

    PHYSICAL_ADDRESS pa = index << PAGE_SHIFT;
    if (hw->NotifyFrame != NULL)
        hw->NotifyFrame(hw->Context, pa, (UCHAR)State);
    if (Zero)
        memset(hw->PhysicalToPointer(hw->Context, pa, PAGE_SIZE), 0, PAGE_SIZE);
    *Out = pa;
    return STATUS_SUCCESS;
}

ULONG MiDereferenceFrame(MM_FRAME_DB *Db, PHYSICAL_ADDRESS Physical, MM_FRAME_STATE ExpectedState)
{
    const MM_HW_OPS *hw = Db->Hw;
    if ((Physical & (PAGE_SIZE - 1u)) != 0 || (Physical >> PAGE_SHIFT) >= Db->FrameCount)
        MI_STOP(MM_STOP_FRAME_BAD_ADDRESS, Physical, 0, 0);
    ULONG64  index = Physical >> PAGE_SHIFT;
    unsigned bad   = 0;
    ULONG    left  = 0;
    BOOLEAN  freed = false;

    hw->AcquireLock(hw->Context, LOCK_RANK_FRAME_DB, &Db->Lock);
    MM_PFN *p = &Db->Pfn[index];
    if (p->State == MmFrameFree)
        bad = MM_STOP_FRAME_DOUBLE_FREE;
    else if (p->State != ExpectedState || p->RefCount == 0)
        bad = MM_STOP_FRAME_BAD_STATE;
    else {
        left = --p->RefCount;
        if (left == 0) {
            MiSetState(Db, index, MmFrameFree, 0);
            Db->Bitmap[index / 64u] |= UINT64_C(1) << (index % 64u);
            freed = true;
        }
    }
    UCHAR seen = p->State;
    hw->ReleaseLock(hw->Context, LOCK_RANK_FRAME_DB, &Db->Lock);
    if (bad != 0)
        MI_STOP(bad, Physical, seen, ExpectedState);
    if (freed && hw->NotifyFrame != NULL)
        hw->NotifyFrame(hw->Context, Physical, MmFrameFree);
    return left;
}

void MiReferenceFrame(MM_FRAME_DB *Db, PHYSICAL_ADDRESS Physical)
{
    const MM_HW_OPS *hw = Db->Hw;
    if ((Physical & (PAGE_SIZE - 1u)) != 0 || (Physical >> PAGE_SHIFT) >= Db->FrameCount)
        MI_STOP(MM_STOP_FRAME_BAD_ADDRESS, Physical, 0, 0);
    unsigned bad = 0;
    hw->AcquireLock(hw->Context, LOCK_RANK_FRAME_DB, &Db->Lock);
    MM_PFN *p = &Db->Pfn[Physical >> PAGE_SHIFT];
    if (p->State != MmFrameActive)
        bad = MM_STOP_FRAME_BAD_STATE;
    else if (p->RefCount == 0xFFFFFFFFu)
        bad = MM_STOP_FRAME_REFCOUNT;
    else
        p->RefCount++;
    UCHAR seen = p->State;
    hw->ReleaseLock(hw->Context, LOCK_RANK_FRAME_DB, &Db->Lock);
    if (bad != 0)
        MI_STOP(bad, Physical, seen, 0);
}

const MM_PFN *MiGetFrame(const MM_FRAME_DB *Db, PHYSICAL_ADDRESS Physical)
{
    if ((Physical & (PAGE_SIZE - 1u)) != 0 || (Physical >> PAGE_SHIFT) >= Db->FrameCount)
        return NULL;
    return &Db->Pfn[Physical >> PAGE_SHIFT];
}

ULONG64 MiFrameDbVerify(const MM_FRAME_DB *Db)
{
    ULONG64 defects                   = 0;
    ULONG64 counts[MmFrameStateCount] = {0};
    for (ULONG64 f = 0; f < Db->FrameCount; f++) {
        const MM_PFN *p   = &Db->Pfn[f];
        BOOLEAN       bit = (Db->Bitmap[f / 64u] >> (f % 64u)) & 1u;
        if (p->State >= MmFrameStateCount) {
            defects++;
            continue;
        }
        counts[p->State]++;
        if (bit != (p->State == MmFrameFree))
            defects++;
        if (p->State == MmFrameFree && p->RefCount != 0)
            defects++;
        if ((p->State == MmFrameActive || p->State == MmFramePageTable) && p->RefCount == 0)
            defects++;
        if (p->State == MmFramePageTable && p->RefCount != 1)
            defects++;
    }
    for (unsigned s = 0; s < MmFrameStateCount; s++)
        if (counts[s] != Db->StateCount[s])
            defects++;
    for (ULONG64 w = (Db->FrameCount + 63u) / 64u; w < Db->BitmapWords; w++)
        if (Db->Bitmap[w] != 0)
            defects++;
    if (Db->FrameCount % 64u != 0 && Db->BitmapWords != 0 &&
        (Db->Bitmap[Db->BitmapWords - 1u] >> (Db->FrameCount % 64u)) != 0)
        defects++;
    return defects;
}

/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Boot allocator and frame database. Owner: A04. Internal to ntoskrnl/mm/.
 *
 * Order of use (docs/architecture/memory.md):
 *   MiBootAllocatorInit  builds the usable ranges (boot map minus reservations)
 *   MiBootAllocatePages  optional early allocations before the database exists
 *   MiFrameDbInit        places the database in boot memory, classifies every frame, closes the
 *                        boot allocator
 *   MiAllocateFrame / MiDereferenceFrame / MiReferenceFrame afterwards
 *
 * Lock: the frame database lock has rank LOCK_RANK_FRAME_DB (70, spinlock, DISPATCH_LEVEL).
 */
#ifndef KAMOS_NTOSKRNL_MM_FRAME_H
#define KAMOS_NTOSKRNL_MM_FRAME_H

#include <kamos/boot.h>

#include "mm_hw.h"

typedef enum _MM_FRAME_STATE {
    MmFrameNonexistent = 0, /* Not in the memory map. Never touched. */
    MmFrameFree        = 1,
    MmFrameActive      = 2, /* Allocated through MmAllocatePhysicalPage. */
    MmFramePageTable   = 3, /* Owned by the page-table code. */
    MmFrameAcpi        = 4, /* ACPI reclaimable. Not allocatable until released explicitly. */
    MmFrameReserved    = 5, /* Kernel image, modules, first MiB, NVS, framebuffer, database. */
    MmFrameBad         = 6,
    MmFrameStateCount
} MM_FRAME_STATE;

#define MM_PFN_FLAG_BOOT     0x01u /* Taken by the boot allocator. */
#define MM_PFN_FLAG_DATABASE 0x02u /* Holds the frame database itself. */

typedef struct _MM_PFN {
    UCHAR  State; /* MM_FRAME_STATE */
    UCHAR  Flags;
    USHORT Spare;
    ULONG  RefCount; /* 0 when free, 1 or more when ACTIVE, 1 when PAGETABLE. */
} MM_PFN;
KAMOS_STATIC_ASSERT(sizeof(MM_PFN) == 8, "MM_PFN is 8 bytes");

/* A physical range in page frame numbers: [First, End). */
typedef struct _MM_PFN_RANGE {
    ULONG64 First;
    ULONG64 End;
} MM_PFN_RANGE;

/* An extra reservation given by the caller (kernel image, loader structure). Bytes. */
typedef struct _MM_RESERVED_RANGE {
    PHYSICAL_ADDRESS Base;
    ULONG64          Length;
} MM_RESERVED_RANGE;

#define MM_MAX_EXTRA_RESERVATIONS 8u
#define MM_MAX_RESERVATIONS       (1u + 1u + BOOT_MAX_MODULES + 1u + 1u + MM_MAX_EXTRA_RESERVATIONS)
#define MM_MAX_BOOT_RANGES        (BOOT_MAX_MEMORY_REGIONS + MM_MAX_RESERVATIONS + 8u)
#define MM_MAX_PHYSICAL_BITS      52u
#define MM_FIRST_MIB_PAGES        256u

typedef struct _MM_FRAME_DB {
    const MM_HW_OPS        *Hw;
    MM_PFN                 *Pfn;
    ULONG64                *Bitmap;     /* Bit set = frame is MmFrameFree. */
    ULONG64                 FrameCount; /* Frames 0 .. FrameCount-1 are described. */
    ULONG64                 BitmapWords;
    ULONG64                 Hint; /* Word index where the next search starts. */
    ULONG64                 StateCount[MmFrameStateCount];
    PHYSICAL_ADDRESS        DbPhysical;
    ULONG64                 DbPages;
    BOOLEAN                 BootClosed;
    BOOLEAN                 Initialised;
    ULONG                   BootRangeCount;
    MM_PFN_RANGE            BootRanges[MM_MAX_BOOT_RANGES];
    ULONG                   ReservationCount;
    MM_PFN_RANGE            Reservations[MM_MAX_RESERVATIONS]; /* Rounded outward. */
    const BOOT_INFORMATION *Boot;
    char                    Lock; /* Address identifies the lock to the hooks. */
} MM_FRAME_DB;

/*
 * Validates the boot map (sorted, no overlap, known types, whole usable memory, limits) and
 * stops with KAMOS_STOP_BAD_BOOT_INFORMATION if it is not. Builds the usable ranges: usable
 * regions shrunk to whole pages, minus the first MiB, the modules, the framebuffer, the page of
 * the ACPI root pointer, and the Extra ranges (rounded outward).
 */
void MiBootAllocatorInit(MM_FRAME_DB *Db, const MM_HW_OPS *Hw, const BOOT_INFORMATION *Boot,
                         const MM_RESERVED_RANGE *Extra, ULONG ExtraCount);

/* Contiguous pages from the highest range that fits. STATUS_NO_MEMORY if none. */
NTSTATUS MiBootAllocatePages(MM_FRAME_DB *Db, ULONG64 Count, PHYSICAL_ADDRESS *Out);

void MiFrameDbInit(MM_FRAME_DB *Db);

/* Zeroed on request. STATUS_NO_MEMORY when no frame is free. State: Active or PageTable. */
NTSTATUS MiAllocateFrame(MM_FRAME_DB *Db, MM_FRAME_STATE State, BOOLEAN Zero,
                         PHYSICAL_ADDRESS *Out);

/*
 * Drops one reference of a frame that must be in ExpectedState. When the count reaches zero the
 * frame is free. Returns the new count. Unknown address, unaligned address, wrong state and
 * double free stop the kernel.
 */
ULONG MiDereferenceFrame(MM_FRAME_DB *Db, PHYSICAL_ADDRESS Physical, MM_FRAME_STATE ExpectedState);

/* Adds a reference to an Active frame. Stops on any other state or on overflow. */
void MiReferenceFrame(MM_FRAME_DB *Db, PHYSICAL_ADDRESS Physical);

/* Read-only view of a frame, NULL if the address is unaligned or beyond the database. */
const MM_PFN *MiGetFrame(const MM_FRAME_DB *Db, PHYSICAL_ADDRESS Physical);

/* Cross-checks bitmap, states, counts and reference counts. Returns the number of defects. */
ULONG64 MiFrameDbVerify(const MM_FRAME_DB *Db);

#endif /* KAMOS_NTOSKRNL_MM_FRAME_H */

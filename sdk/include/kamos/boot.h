/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Boot information handed over by the boot loader (ADR-0001, RFC-0001).
 * Owner: A03. Contract state: provisional until the Wave 1 gate.
 *
 * A03 validates the loader's structure once, in Phase 1, and copies what it needs into the
 * fixed-size storage below before any allocator exists. The loader's own memory is then free
 * to be reused. Nothing here is ever allocated dynamically.
 *
 * The memory map is normalised by A03 in three ways: sorted by Base, zero-length entries
 * dropped, and entries of the SAME type that overlap or touch merged into one. Entries of
 * DIFFERENT types that overlap are not repaired. The kernel image, the boot modules, the boot
 * information structure and the first MiB are NOT removed from it: A04 reserves them when it
 * builds the frame database. A map that does not fit the limits below, has overlapping entries
 * of different types, has a Base + Length that wraps past 2^64, or lacks usable memory stops the
 * kernel with KAMOS_STOP_BAD_BOOT_INFORMATION; it is never silently truncated. A real PC whose
 * map overlaps in this way does not boot: revisit at milestone M-HW.
 */
#ifndef KAMOS_BOOT_H
#define KAMOS_BOOT_H

#include <kamos/status.h>
#include <kamos/types.h>

#define BOOT_MAX_MEMORY_REGIONS 128u
#define BOOT_MAX_MODULES        8u
#define BOOT_MODULE_NAME_MAX    32u

typedef enum _BOOT_MEMORY_TYPE {
    BootMemoryUsable          = 1,
    BootMemoryReserved        = 2,
    BootMemoryAcpiReclaimable = 3,
    BootMemoryAcpiNvs         = 4,
    BootMemoryBad             = 5,
} BOOT_MEMORY_TYPE;

typedef struct _BOOT_MEMORY_REGION {
    PHYSICAL_ADDRESS Base;
    ULONG64          Length;      /* Bytes. */
    BOOT_MEMORY_TYPE Type;
} BOOT_MEMORY_REGION;

typedef struct _BOOT_MODULE {
    PHYSICAL_ADDRESS Base;
    ULONG64          Length;      /* Bytes. */
    char             Name[BOOT_MODULE_NAME_MAX];   /* NUL-terminated, from the loader command line. */
} BOOT_MODULE;

typedef struct _BOOT_FRAMEBUFFER {
    BOOLEAN          Present;
    UCHAR            BitsPerPixel;
    ULONG            Width;
    ULONG            Height;
    ULONG            Pitch;       /* Bytes per scanline. */
    PHYSICAL_ADDRESS Base;
} BOOT_FRAMEBUFFER;

typedef struct _BOOT_INFORMATION {
    ULONG              RegionCount;
    BOOT_MEMORY_REGION Regions[BOOT_MAX_MEMORY_REGIONS];
    ULONG              ModuleCount;
    BOOT_MODULE        Modules[BOOT_MAX_MODULES];
    BOOT_FRAMEBUFFER   Framebuffer;
    PHYSICAL_ADDRESS   AcpiRsdp;  /* 0 when the loader gave none. */
} BOOT_INFORMATION;

/*
 * Returns the validated boot information. Any level. Valid for the lifetime of the kernel and
 * read-only. Callable only after A03's Phase 1 initialisation; before that it is a bug check.
 */
const BOOT_INFORMATION *HalGetBootInformation(void);

#endif /* KAMOS_BOOT_H */

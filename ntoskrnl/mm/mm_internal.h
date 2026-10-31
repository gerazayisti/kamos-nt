/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Internal interface of the memory manager. Owner: A04.
 *
 * Every function takes its context explicitly so that the host tests can run several machines.
 * mm_api.c implements the public mm.h functions on a global context.
 */
#ifndef KAMOS_NTOSKRNL_MM_INTERNAL_H
#define KAMOS_NTOSKRNL_MM_INTERNAL_H

#include <kamos/ke.h>

#include "mm_frame.h"
#include "mm_pool.h"

/* ADR-0005. Inclusive ends. Changing a boundary is an RFC. */
#define MI_USER_FIRST        UINT64_C(0x0000000000010000)
#define MI_USER_LAST         UINT64_C(0x00007FFFFFFEFFFF)
#define MI_DIRECT_MAP_BASE   UINT64_C(0xFFFF800000000000)
#define MI_DIRECT_MAP_LAST   UINT64_C(0xFFFF87FFFFFFFFFF)
#define MI_POOL_BASE         UINT64_C(0xFFFFA00000000000)
#define MI_POOL_LAST         UINT64_C(0xFFFFA0FFFFFFFFFF)
#define MI_STACK_BASE        UINT64_C(0xFFFFB00000000000)
#define MI_STACK_LAST        UINT64_C(0xFFFFB00FFFFFFFFF)
#define MI_DEVICE_BASE       UINT64_C(0xFFFFC00000000000)
#define MI_DEVICE_LAST       UINT64_C(0xFFFFC0FFFFFFFFFF)
#define MI_IMAGE_BASE        UINT64_C(0xFFFFFFFF80000000)
#define MI_IMAGE_LAST        UINT64_C(0xFFFFFFFFFFFFFFFF)
#define MI_KERNEL_HALF_FIRST UINT64_C(0xFFFF800000000000)

#define MI_PROT_ALL         (MM_PROT_READ | MM_PROT_WRITE | MM_PROT_EXECUTE | MM_PROT_USER)
#define MI_INVALID_PHYSICAL (~(PHYSICAL_ADDRESS)0)

#define MI_SPACE_MAGIC 0x41445350u /* 'ADSP' */

typedef struct _MM_CONTEXT MM_CONTEXT;

/* Where page-table pages come from. The default takes them from the frame database. */
typedef struct _MM_FRAME_SOURCE {
    void *Context;
    NTSTATUS (*Allocate)(void *Context, PHYSICAL_ADDRESS *Physical); /* Zeroed. */
    void (*Free)(void *Context, PHYSICAL_ADDRESS Physical);
} MM_FRAME_SOURCE;

struct _ADDRESS_SPACE {
    ULONG            Magic;
    BOOLEAN          IsKernel;
    PHYSICAL_ADDRESS Root; /* PML4 frame. */
    MM_CONTEXT      *Mm;
    char             Lock; /* LOCK_RANK_ADDRESS_SPACE. Address identifies the lock. */
};

struct _MM_CONTEXT {
    const MM_HW_OPS *Hw;
    MM_FRAME_DB     *Frames;
    MM_FRAME_SOURCE  TableFrames;
    ULONG            MaxPhysicalBits; /* From CPUID; 36 to 52. */
    ADDRESS_SPACE    KernelSpace;
    ADDRESS_SPACE   *Current;
    MM_POOL          Pool;
};

/* The context used by the Mm* functions of mm.h (mm_api.c). */
void MiSetGlobalContext(MM_CONTEXT *Mm);

/* Sets the default frame source (frame database, PageTable state). */
void MiContextInit(MM_CONTEXT *Mm, const MM_HW_OPS *Hw, MM_FRAME_DB *Frames, ULONG MaxPhysicalBits);

/*
 * Builds the kernel address space. ExistingRoot 0 allocates a new PML4; a non-zero value adopts
 * the tables built by the loader (their kernel-half entries are kept). In both cases every empty
 * kernel-half PML4 entry gets its PDPT, so that the kernel half never changes again and can be
 * copied into new spaces without a lock.
 */
NTSTATUS MiInitKernelSpace(MM_CONTEXT *Mm, PHYSICAL_ADDRESS ExistingRoot);

NTSTATUS MiCreateAddressSpace(MM_CONTEXT *Mm, ADDRESS_SPACE **Space);
void     MiDestroyAddressSpace(ADDRESS_SPACE *Space);
void     MiSwitchAddressSpace(ADDRESS_SPACE *Space);

/*
 * Map, unmap, protect: all or nothing. Errors are those documented in mm.h; an unmapped page in
 * Unmap or Protect is STATUS_INVALID_PARAMETER, a large page is STATUS_NOT_IMPLEMENTED.
 * Addresses are passed as integers; the public wrappers cast.
 */
NTSTATUS MiMapPages(ADDRESS_SPACE *Space, ULONG_PTR Virtual, PHYSICAL_ADDRESS Physical,
                    SIZE_T Count, ULONG Prot);
NTSTATUS MiUnmapPages(ADDRESS_SPACE *Space, ULONG_PTR Virtual, SIZE_T Count);
NTSTATUS MiProtectPages(ADDRESS_SPACE *Space, ULONG_PTR Virtual, SIZE_T Count, ULONG Prot);

/*
 * Walks the tables without locking (callers rely on the mapping being stable). Handles 4 KiB,
 * 2 MiB and 1 GiB pages. Physical includes the page offset. Prot is the effective protection
 * (W and USER are the AND over the levels, EXECUTE the negation of the OR of XD).
 */
NTSTATUS MiTranslate(const ADDRESS_SPACE *Space, ULONG_PTR Virtual, PHYSICAL_ADDRESS *Physical,
                     ULONG *Prot);

/* Maps every usable and ACPI page of the boot map into the direct map (read/write, no execute). */
NTSTATUS MiBuildDirectMap(ADDRESS_SPACE *Kernel, const BOOT_INFORMATION *Boot);

/* True if [Address, Address+Length) lies wholly in the user range (no wrap, no guard page). */
BOOLEAN MiIsUserRange(ULONG_PTR Address, SIZE_T Length);

/* True if the address is in the direct map; the physical address is Address - base. */
BOOLEAN MiIsDirectMap(ULONG_PTR Address);

/* Page-table pages in the structure rooted at Root (diagnostic; the host tests cross-check). */
ULONG64 MiCountTables(const MM_CONTEXT *Mm, PHYSICAL_ADDRESS Root, BOOLEAN KernelHalfToo);

#endif /* KAMOS_NTOSKRNL_MM_INTERNAL_H */

/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Kernel pool. Owner: A04. Internal to ntoskrnl/mm/.
 *
 * Small requests come from size classes carved out of single pages; large requests take whole
 * pages from the page provider. Every block has a 32-byte header in front of the payload.
 * Lock: LOCK_RANK_POOL (60, spinlock). The lock is never held across a call to the page
 * provider (the provider takes the address-space lock, rank 30).
 */
#ifndef KAMOS_NTOSKRNL_MM_POOL_H
#define KAMOS_NTOSKRNL_MM_POOL_H

#include "mm_hw.h"

/* Where the pool gets its pages (kernel: pool region + frames + MmMapPages). */
typedef struct _MM_PAGE_PROVIDER {
    void *Context;
    NTSTATUS (*GetPages)(void *Context, SIZE_T Count, void **Virtual); /* Page aligned. */
    void (*PutPages)(void *Context, void *Virtual, SIZE_T Count);
} MM_PAGE_PROVIDER;

#define MM_POOL_CLASS_COUNT 5u /* Block sizes 64, 128, 256, 512, 1024. */
#define MM_POOL_TYPES       2u
#define MM_POOL_MAX_TAGS    128u
#define MM_POOL_HEADER_SIZE 32u
#define MM_POOL_TRAILER     8u
#define MM_POOL_ALIGNMENT   16u

typedef struct _MM_POOL_TAG_INFO {
    ULONG   Tag;
    ULONG64 Allocations;
    ULONG64 Frees;
    ULONG64 BytesInUse; /* Sum of requested sizes of live blocks. */
    ULONG64 PeakBytes;
} MM_POOL_TAG_INFO;

typedef struct _MM_POOL {
    const MM_HW_OPS *Hw;
    MM_PAGE_PROVIDER Provider;
    BOOLEAN          Debug; /* Fill patterns, trailer canary, free-fill check. */
    void            *FreeList[MM_POOL_TYPES][MM_POOL_CLASS_COUNT];
    void            *Slabs; /* Chain of pages carved into blocks. */
    ULONG64          SlabPages;
    ULONG64          LargePages;
    ULONG64          Live; /* Live allocations. */
    ULONG            TagCount;
    MM_POOL_TAG_INFO Tags[MM_POOL_MAX_TAGS];
    MM_POOL_TAG_INFO Other; /* Tags that did not fit in the table. */
    char             Lock;
} MM_POOL;

void MiPoolInit(MM_POOL *Pool, const MM_HW_OPS *Hw, const MM_PAGE_PROVIDER *Provider,
                BOOLEAN Debug);

/* NULL on failure, on a zero size, on a size that overflows and on a bad tag. */
void *MiPoolAllocate(MM_POOL *Pool, POOL_TYPE Type, SIZE_T Size, ULONG Tag);

/* Stops with KAMOS_STOP_POOL_CORRUPTION on a bad pointer, double free, wrong tag or type. */
void MiPoolFree(MM_POOL *Pool, void *Block, ULONG Tag);

/* Accounting by tag. Index 0 .. MiPoolTagCount-1. */
ULONG   MiPoolTagCount(const MM_POOL *Pool);
BOOLEAN MiPoolGetTag(const MM_POOL *Pool, ULONG Index, MM_POOL_TAG_INFO *Info);
BOOLEAN MiPoolFindTag(const MM_POOL *Pool, ULONG Tag, MM_POOL_TAG_INFO *Info);

/* Gives the slab pages back to the provider. Stops if allocations are still live. */
void MiPoolReleaseAll(MM_POOL *Pool);

/* Reasons in parameter 1 of KAMOS_STOP_POOL_CORRUPTION. */
enum {
    MM_POOL_STOP_BAD_POINTER = 1,
    MM_POOL_STOP_DOUBLE_FREE,
    MM_POOL_STOP_BAD_HEADER,
    MM_POOL_STOP_WRONG_TAG,
    MM_POOL_STOP_OVERRUN,
    MM_POOL_STOP_WRITE_AFTER_FREE,
    MM_POOL_STOP_ACCOUNTING,
    MM_POOL_STOP_LIVE_AT_RELEASE,
};

/* Largest payload served from a size class. */
#define MM_POOL_SMALL_MAX (1024u - MM_POOL_HEADER_SIZE - MM_POOL_TRAILER)

#endif /* KAMOS_NTOSKRNL_MM_POOL_H */

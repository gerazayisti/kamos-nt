/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Hardware abstraction seam of the memory manager. Owner: A04.
 *
 * The memory manager reaches the machine only through this table. The kernel fills it in
 * (direct map, invlpg, CR3, KeAcquireSpinLock...); the host tests fill it with a simulated
 * physical memory (tests/mm/host/mm_host.c). Nothing else in ntoskrnl/mm/ is architecture or
 * host specific.
 */
#ifndef KAMOS_NTOSKRNL_MM_HW_H
#define KAMOS_NTOSKRNL_MM_HW_H

#include <kamos/bugcheck.h>
#include <kamos/mm.h>
#include <kamos/status.h>
#include <kamos/types.h>

typedef struct _MM_HW_OPS {
    void *Context;

    /*
     * Returns a pointer to Length bytes of physical memory. An access to memory that does not
     * exist must not return (the kernel direct map faults; the host simulator stops). Required.
     */
    void *(*PhysicalToPointer)(void *Context, PHYSICAL_ADDRESS Physical, SIZE_T Length);

    /* Flushes the translation of one page on the current processor (invlpg). Required. */
    void (*InvalidatePage)(void *Context, ULONG_PTR Virtual);

    /* Root of the paging structures loaded on the current processor (CR3 without flags). */
    PHYSICAL_ADDRESS (*GetCurrentRoot)(void *Context);
    void (*SetCurrentRoot)(void *Context, PHYSICAL_ADDRESS Root);

    /*
     * Lock hooks. Rank is a LOCK_RANK_* value (ke.h); Lock identifies the instance. The kernel
     * maps ranks 30 to KeAcquireMutex and 60/70 to KeAcquireSpinLock. The host checks the order.
     */
    void (*AcquireLock)(void *Context, ULONG Rank, const void *Lock);
    void (*ReleaseLock)(void *Context, ULONG Rank, const void *Lock);

    /* Current execution level (irql.h): 0 is PASSIVE_LEVEL. Used by debug checks only. */
    UCHAR (*GetCurrentIrql)(void *Context);

    /* Optional. Called after a frame changes state, so a simulator can poison free frames. */
    void (*NotifyFrame)(void *Context, PHYSICAL_ADDRESS Physical, UCHAR NewState);
} MM_HW_OPS;

/*
 * Stop codes. Frame database and page-table corruption have no code of their own in bugcheck.h
 * yet (request to A01): they use KAMOS_STOP_ASSERTION_FAILED with MM_STOP_BASE | reason in
 * parameter 1. These are integrity checks, never ASSERT: they stop in every build.
 */
#define MM_STOP_BASE 0x4D4D0000u

enum {
    MM_STOP_FRAME_DOUBLE_FREE = 1,
    MM_STOP_FRAME_BAD_STATE,
    MM_STOP_FRAME_BAD_ADDRESS,
    MM_STOP_FRAME_REFCOUNT,
    MM_STOP_BOOT_ALLOC_CLOSED,
    MM_STOP_BOOT_ALLOC_EXHAUSTED,
    MM_STOP_SPACE_BAD,
    MM_STOP_SPACE_DESTROY_CURRENT,
    MM_STOP_SPACE_DESTROY_KERNEL,
    MM_STOP_TABLE_CORRUPT,
};

/* Reasons passed in parameter 1 of KAMOS_STOP_BAD_BOOT_INFORMATION by the memory manager. */
enum {
    MM_BOOT_BAD_REGION_COUNT = 0x4D01,
    MM_BOOT_BAD_REGION_TYPE,
    MM_BOOT_REGION_OVERFLOW,
    MM_BOOT_NOT_SORTED,
    MM_BOOT_OVERLAP,
    MM_BOOT_NO_USABLE_MEMORY,
    MM_BOOT_BAD_RESERVATION,
    MM_BOOT_TOO_MANY_RANGES,
    MM_BOOT_DB_NO_ROOM,
    MM_BOOT_TOO_MANY_FRAMES,
};

#define MI_STOP(Reason, A, B, C)                                                                   \
    KeBugCheckEx(KAMOS_STOP_ASSERTION_FAILED, (ULONG_PTR)(MM_STOP_BASE | (Reason)),                \
                 (ULONG_PTR)(A), (ULONG_PTR)(B), (ULONG_PTR)(C))

#define MI_BOOT_STOP(Reason, A, B, C)                                                              \
    KeBugCheckEx(KAMOS_STOP_BAD_BOOT_INFORMATION, (ULONG_PTR)(Reason), (ULONG_PTR)(A),             \
                 (ULONG_PTR)(B), (ULONG_PTR)(C))

#endif /* KAMOS_NTOSKRNL_MM_HW_H */

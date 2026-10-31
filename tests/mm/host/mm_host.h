/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Host test support for the memory manager. Owner: A04.
 *
 * - simulated physical memory that backs only the pages the test machine really has
 * - hardware seam (MM_HW_OPS) implemented on top of it
 * - stop capture: KeBugCheckEx longjmps back to the test
 * - lock-order checker, invlpg log
 * - brute-force classification of a boot map, written without the memory manager's helpers
 *
 * Linked through tests/mm/host/mm_all.c, which also includes the sources under test.
 */
#ifndef KAMOS_TESTS_MM_HOST_H
#define KAMOS_TESTS_MM_HOST_H

#include <setjmp.h>
#include <stdint.h>

#include "../../../ntoskrnl/mm/mm_internal.h"

/* ---- stop capture ---- */
typedef struct HostStop {
    volatile int       armed;
    jmp_buf            jb;
    volatile unsigned  count; /* stops since the last HostStopClear */
    volatile int       code;  /* KAMOS_STOP_CODE */
    volatile ULONG_PTR p[4];
    volatile unsigned  lockDepthAtStop;
} HostStop;
extern HostStop g_stop;

void HostStopClear(void);
void HostStopArm(void);
void HostStopDisarm(void);

/* Runs the statements; sets var to 1 if the kernel stopped, 0 otherwise. */
#define HOST_CATCH(var, ...)                                                                       \
    do {                                                                                           \
        HostStopClear();                                                                           \
        HostStopArm();                                                                             \
        if (setjmp(g_stop.jb) == 0) {                                                              \
            __VA_ARGS__;                                                                           \
            (var) = 0;                                                                             \
        } else {                                                                                   \
            (var) = 1;                                                                             \
        }                                                                                          \
        HostStopDisarm();                                                                          \
    } while (0)

/* ---- lock checker ---- */
#define HOST_MAX_HELD 16u
typedef struct HostLocks {
    ULONG       rank[HOST_MAX_HELD];
    const void *obj[HOST_MAX_HELD];
    unsigned    depth;
    unsigned    violations;
    unsigned    acquired[128]; /* per rank */
    unsigned    total;
    const void *last[128]; /* last object acquired at each rank */
} HostLocks;
extern HostLocks g_locks;
void             HostLocksReset(void);

/* ---- hardware state ---- */
extern MM_HW_OPS        g_hw;
extern PHYSICAL_ADDRESS g_hostRoot;
extern UCHAR            g_hostIrql;
extern unsigned         g_simWild; /* wild physical accesses seen */
extern ULONG_PTR       *g_inval;   /* addresses passed to InvalidatePage */
extern size_t           g_invalCount;
void                    HostInvalClear(void);

/* ---- simulated physical memory ---- */
void SimReset(void);
void SimBackPages(ULONG64 firstPfn, ULONG64 endPfn);

/* ---- machines ---- */
typedef struct HostMachine {
    BOOT_INFORMATION  boot;
    MM_RESERVED_RANGE extra[MM_MAX_EXTRA_RESERVATIONS];
    ULONG             extraCount;
    MM_FRAME_DB       db;
    MM_CONTEXT        mm;
} HostMachine;

void HostMachineStandard(HostMachine *m); /* about 47 MiB of RAM, holes, ACPI, reservations */
void HostMachineSmall(HostMachine *m, ULONG64 usableMiB); /* usable memory from 1 MiB */
void HostAddRegion(HostMachine *m, PHYSICAL_ADDRESS base, ULONG64 length, BOOT_MEMORY_TYPE type);

/* Everything: frames, kernel space, pool with the host page provider. */
void HostInitMm(HostMachine *m, int debugPool);
void HostTeardown(HostMachine *m);

/* Fault injection: number of allocations that still succeed, -1 = never fail. */
extern long     g_tableFailAfter;    /* page-table frames */
extern unsigned g_tableAllocs;       /* attempts since HostInitMm */
extern long     g_poolFailAfter;     /* pages from the pool's provider */
extern long     g_poolPages;         /* provider pages outstanding */
extern unsigned g_providerLockDepth; /* max locks held when the pool called its provider */
extern int
    g_poolQuarantine; /* keep freed provider pages mapped (to test double free of large blocks) */

/* Backs exactly the pages that may become frames (brute force), resets the simulator. */
void HostBackMachine(HostMachine *m);
/* Frame database only. */
void HostInitFrames(HostMachine *m);

/*
 * Expected classification of frame Pfn, computed page by page from the byte ranges of the map
 * and of the reservations, sharing no code with the memory manager.
 */
typedef enum HostExpect {
    HostExpectNone = 0, /* not in any region (or only partly usable) */
    HostExpectUsable,   /* free, unless the boot allocator took it */
    HostExpectAcpi,
    HostExpectReserved,
    HostExpectBad,
} HostExpect;
HostExpect HostClassify(const HostMachine *m, ULONG64 pfn);

/* Compares every frame with the classification. Returns the number of differences. */
ULONG64 HostCheckFrames(const HostMachine *m);

/* Copy of the frame database, to prove that a failed operation changed nothing. */
typedef struct HostSnap {
    MM_PFN  *pfn;
    ULONG64 *bm;
    ULONG64  counts[MmFrameStateCount];
    ULONG64  frames, words;
} HostSnap;
void HostSnapTake(const MM_FRAME_DB *db, HostSnap *s);
int  HostSnapSame(const MM_FRAME_DB *db, const HostSnap *s);
void HostSnapFree(HostSnap *s);

/* xorshift64* generator, seed printed on failure by the caller. */
typedef struct HostRng {
    uint64_t s;
} HostRng;
static inline uint64_t HostRand(HostRng *r)
{
    r->s ^= r->s >> 12;
    r->s ^= r->s << 25;
    r->s ^= r->s >> 27;
    return r->s * UINT64_C(0x2545F4914F6CDD1D);
}
static inline uint64_t HostRandBelow(HostRng *r, uint64_t n)
{
    return n == 0 ? 0 : HostRand(r) % n;
}

#endif /* KAMOS_TESTS_MM_HOST_H */

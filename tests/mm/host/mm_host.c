/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Host test support for the memory manager. Owner: A04. See mm_host.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mm_host.h"

#if defined(__SANITIZE_ADDRESS__)
#define HOST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HOST_ASAN 1
#endif
#endif
#ifdef HOST_ASAN
#include <sanitizer/asan_interface.h>
#endif

/* ------------------------------------------------------------------ stops */

HostStop g_stop;

void HostStopClear(void)
{
    g_stop.count = 0;
    g_stop.code  = 0;
    for (int i = 0; i < 4; i++)
        g_stop.p[i] = 0;
    g_stop.lockDepthAtStop = 0;
}

void HostStopArm(void)
{
    g_stop.armed = 1;
}

void HostStopDisarm(void)
{
    g_stop.armed = 0;
    HostLocksReset(); /* A stop leaves whatever was held; the system would be halted. */
}

_Noreturn void KeBugCheckEx(KAMOS_STOP_CODE Code, ULONG_PTR P1, ULONG_PTR P2, ULONG_PTR P3,
                            ULONG_PTR P4)
{
    g_stop.count++;
    g_stop.code            = (int)Code;
    g_stop.p[0]            = P1;
    g_stop.p[1]            = P2;
    g_stop.p[2]            = P3;
    g_stop.p[3]            = P4;
    g_stop.lockDepthAtStop = g_locks.depth;
    if (g_stop.armed)
        longjmp(g_stop.jb, 1);
    fprintf(stderr, "UNEXPECTED STOP code=%d p1=%#lx p2=%#lx p3=%#lx p4=%#lx\n", (int)Code,
            (unsigned long)P1, (unsigned long)P2, (unsigned long)P3, (unsigned long)P4);
    abort();
}

void DbgPrintAssertFailure(const char *Expr, const char *File, int Line)
{
    fprintf(stderr, "ASSERT %s at %s:%d\n", Expr, File, Line);
}

/* ------------------------------------------------------------------ locks */

HostLocks g_locks;

void HostLocksReset(void)
{
    g_locks.depth = 0;
}

static void HostAcquire(void *Context, ULONG Rank, const void *Lock)
{
    (void)Context;
    if (Rank >= 128u || g_locks.depth >= HOST_MAX_HELD ||
        (g_locks.depth > 0 && Rank <= g_locks.rank[g_locks.depth - 1u])) {
        g_locks.violations++;
        if (g_locks.depth >= HOST_MAX_HELD || Rank >= 128u)
            return;
    }
    g_locks.rank[g_locks.depth] = Rank;
    g_locks.obj[g_locks.depth]  = Lock;
    g_locks.depth++;
    g_locks.acquired[Rank]++;
    g_locks.total++;
}

static void HostRelease(void *Context, ULONG Rank, const void *Lock)
{
    (void)Context;
    if (g_locks.depth == 0 || g_locks.rank[g_locks.depth - 1u] != Rank ||
        g_locks.obj[g_locks.depth - 1u] != Lock) {
        g_locks.violations++;
        return;
    }
    g_locks.depth--;
}

/* ------------------------------------------------------------------ simulated memory */

typedef struct SimRegion {
    PHYSICAL_ADDRESS base;
    ULONG64          length;
    UCHAR           *mem;
} SimRegion;

#define SIM_MAX_REGIONS 4096u
static SimRegion g_sim[SIM_MAX_REGIONS];
static unsigned  g_simCount;
unsigned         g_simWild;

PHYSICAL_ADDRESS g_hostRoot;
UCHAR            g_hostIrql;
ULONG_PTR       *g_inval;
size_t           g_invalCount;
static size_t    g_invalCap;

void HostInvalClear(void)
{
    g_invalCount = 0;
}

void SimReset(void)
{
    for (unsigned i = 0; i < g_simCount; i++) {
#ifdef HOST_ASAN
        ASAN_UNPOISON_MEMORY_REGION(g_sim[i].mem, g_sim[i].length);
#endif
        free(g_sim[i].mem);
        g_sim[i].mem = NULL;
    }
    g_simCount = 0;
    g_simWild  = 0;
    g_hostRoot = 0;
    g_hostIrql = 0;
    HostInvalClear();
    HostLocksReset();
    memset(&g_locks, 0, sizeof g_locks);
}

void SimBackPages(ULONG64 firstPfn, ULONG64 endPfn)
{
    if (firstPfn >= endPfn)
        return;
    /* Coalesce with the previous region when contiguous. */
    ULONG64 base = firstPfn << PAGE_SHIFT;
    ULONG64 len  = (endPfn - firstPfn) << PAGE_SHIFT;
    if (g_simCount > 0 && g_sim[g_simCount - 1].base + g_sim[g_simCount - 1].length == base) {
        SimRegion *r = &g_sim[g_simCount - 1];
        UCHAR     *n = aligned_alloc(PAGE_SIZE, r->length + len);
        if (n == NULL)
            abort();
        memcpy(n, r->mem, r->length);
        memset(n + r->length, 0, len);
        free(r->mem);
        r->mem = n;
        r->length += len;
        return;
    }
    if (g_simCount >= SIM_MAX_REGIONS)
        abort();
    UCHAR *mem = aligned_alloc(PAGE_SIZE, len);
    if (mem == NULL)
        abort();
    memset(mem, 0, len);
    g_sim[g_simCount].base   = base;
    g_sim[g_simCount].length = len;
    g_sim[g_simCount].mem    = mem;
    g_simCount++;
}

static void *SimPhysicalToPointer(void *Context, PHYSICAL_ADDRESS Physical, SIZE_T Length)
{
    (void)Context;
    for (unsigned i = 0; i < g_simCount; i++) {
        const SimRegion *r = &g_sim[i];
        if (Physical >= r->base && Physical - r->base < r->length &&
            Length <= r->length - (Physical - r->base))
            return r->mem + (Physical - r->base);
    }
    g_simWild++;
    KeBugCheckEx(KAMOS_STOP_ASSERTION_FAILED, 0x57494C44u /* 'WILD' */, Physical, Length, 0);
}

static void SimNotifyFrame(void *Context, PHYSICAL_ADDRESS Physical, UCHAR NewState)
{
    (void)Context;
#ifdef HOST_ASAN
    void *p = SimPhysicalToPointer(NULL, Physical, PAGE_SIZE);
    if (NewState == MmFrameFree)
        ASAN_POISON_MEMORY_REGION(p, PAGE_SIZE);
    else
        ASAN_UNPOISON_MEMORY_REGION(p, PAGE_SIZE);
#else
    (void)Physical;
    (void)NewState;
#endif
}

static void HostInvalidatePage(void *Context, ULONG_PTR Virtual)
{
    (void)Context;
    if (g_invalCount == g_invalCap) {
        g_invalCap = g_invalCap ? g_invalCap * 2 : 1024;
        g_inval    = realloc(g_inval, g_invalCap * sizeof *g_inval);
        if (g_inval == NULL)
            abort();
    }
    g_inval[g_invalCount++] = Virtual;
}

static PHYSICAL_ADDRESS HostGetRoot(void *Context)
{
    (void)Context;
    return g_hostRoot;
}

static void HostSetRoot(void *Context, PHYSICAL_ADDRESS Root)
{
    (void)Context;
    g_hostRoot = Root;
}

static UCHAR HostGetIrql(void *Context)
{
    (void)Context;
    return g_hostIrql;
}

MM_HW_OPS g_hw = {
    .Context           = NULL,
    .PhysicalToPointer = SimPhysicalToPointer,
    .InvalidatePage    = HostInvalidatePage,
    .GetCurrentRoot    = HostGetRoot,
    .SetCurrentRoot    = HostSetRoot,
    .AcquireLock       = HostAcquire,
    .ReleaseLock       = HostRelease,
    .GetCurrentIrql    = HostGetIrql,
    .NotifyFrame       = SimNotifyFrame,
};

/* ------------------------------------------------------------------ machines */

void HostAddRegion(HostMachine *m, PHYSICAL_ADDRESS base, ULONG64 length, BOOT_MEMORY_TYPE type)
{
    BOOT_MEMORY_REGION *r = &m->boot.Regions[m->boot.RegionCount++];
    r->Base               = base;
    r->Length             = length;
    r->Type               = type;
}

void HostMachineStandard(HostMachine *m)
{
    memset(m, 0, sizeof *m);
    HostAddRegion(m, 0x0, 0x9F000, BootMemoryUsable);
    HostAddRegion(m, 0x9F000, 0x61000, BootMemoryReserved);
    HostAddRegion(m, 0x100000, 0xF00000, BootMemoryUsable); /* to 16 MiB */
    HostAddRegion(m, 0x1000000, 0x10000, BootMemoryAcpiReclaimable);
    HostAddRegion(m, 0x1010000, 0x10000, BootMemoryAcpiNvs);
    HostAddRegion(m, 0x1020000, 0xFE0000, BootMemoryUsable); /* to 32 MiB */
    HostAddRegion(m, 0x2000000, 0x800000, BootMemoryUsable); /* 32 to 40 MiB */
    HostAddRegion(m, 0xFEE00000, 0x1000, BootMemoryReserved);
    m->extra[0].Base          = 0x200000; /* kernel image */
    m->extra[0].Length        = 0x100000;
    m->extra[1].Base          = 0x1800000; /* loader structure: unaligned length */
    m->extra[1].Length        = 0x2345;
    m->extraCount             = 2;
    m->boot.ModuleCount       = 1;
    m->boot.Modules[0].Base   = 0x400000;
    m->boot.Modules[0].Length = 0x10123;
    memcpy(m->boot.Modules[0].Name, "m0", 3);
    m->boot.Framebuffer.Present      = true;
    m->boot.Framebuffer.BitsPerPixel = 32;
    m->boot.Framebuffer.Width        = 640;
    m->boot.Framebuffer.Height       = 100;
    m->boot.Framebuffer.Pitch        = 2560;
    m->boot.Framebuffer.Base         = 0xE0000000;
    m->boot.AcpiRsdp                 = 0xE0000;
}

void HostMachineSmall(HostMachine *m, ULONG64 usableMiB)
{
    memset(m, 0, sizeof *m);
    HostAddRegion(m, 0x0, 0x9F000, BootMemoryUsable);
    HostAddRegion(m, 0x9F000, 0x61000, BootMemoryReserved);
    HostAddRegion(m, 0x100000, usableMiB << 20, BootMemoryUsable);
}

/* Independent classification: byte-range overlap, one page at a time. */
static int Overlaps(uint64_t aBase, uint64_t aEnd, uint64_t bBase, uint64_t bEnd)
{
    return aBase < bEnd && bBase < aEnd;
}

HostExpect HostClassify(const HostMachine *m, ULONG64 pfn)
{
    uint64_t ps  = pfn * 4096u;
    uint64_t pe  = ps + 4096u;
    int      bad = 0, res = 0, acpi = 0, full = 0;
    for (ULONG i = 0; i < m->boot.RegionCount; i++) {
        const BOOT_MEMORY_REGION *r = &m->boot.Regions[i];
        uint64_t                  b = r->Base, e = r->Base + r->Length;
        if (r->Length == 0)
            continue;
        int ovl = Overlaps(ps, pe, b, e);
        switch (r->Type) {
        case BootMemoryBad:
            bad |= ovl;
            break;
        case BootMemoryReserved:
        case BootMemoryAcpiNvs:
            res |= ovl;
            break;
        case BootMemoryAcpiReclaimable:
            acpi |= ovl;
            break;
        case BootMemoryUsable:
            full |= (b <= ps && pe <= e);
            break;
        }
    }
    res |= Overlaps(ps, pe, 0, 0x100000);
    for (ULONG i = 0; i < m->boot.ModuleCount; i++)
        res |= Overlaps(ps, pe, m->boot.Modules[i].Base,
                        m->boot.Modules[i].Base + m->boot.Modules[i].Length);
    if (m->boot.Framebuffer.Present)
        res |= Overlaps(ps, pe, m->boot.Framebuffer.Base,
                        m->boot.Framebuffer.Base +
                            (uint64_t)m->boot.Framebuffer.Pitch * m->boot.Framebuffer.Height);
    if (m->boot.AcpiRsdp != 0) {
        uint64_t rb = m->boot.AcpiRsdp / 4096u * 4096u;
        res |= Overlaps(ps, pe, rb, rb + 4096u);
    }
    for (ULONG i = 0; i < m->extraCount; i++)
        res |= Overlaps(ps, pe, m->extra[i].Base, m->extra[i].Base + m->extra[i].Length);
    if (bad)
        return HostExpectBad;
    if (res)
        return HostExpectReserved;
    if (acpi)
        return HostExpectAcpi;
    if (full)
        return HostExpectUsable;
    return HostExpectNone;
}

static ULONG64 HostHighestUsableEndPfn(const HostMachine *m)
{
    ULONG64 hi = 0;
    for (ULONG i = 0; i < m->boot.RegionCount; i++) {
        const BOOT_MEMORY_REGION *r = &m->boot.Regions[i];
        if (r->Type == BootMemoryUsable && r->Length != 0) {
            ULONG64 e     = (r->Base + r->Length + 4095u) / 4096u;
            ULONG64 first = (r->Base + 4095u) / 4096u;
            ULONG64 last  = (r->Base + r->Length) / 4096u;
            if (first < last && e > hi)
                hi = e;
        }
    }
    return hi;
}

void HostBackMachine(HostMachine *m)
{
    SimReset();
    ULONG64 hi       = HostHighestUsableEndPfn(m);
    ULONG64 runStart = 0;
    int     inRun    = 0;
    for (ULONG64 f = 0; f < hi; f++) {
        int usable = HostClassify(m, f) == HostExpectUsable;
        if (usable && !inRun) {
            runStart = f;
            inRun    = 1;
        } else if (!usable && inRun) {
            SimBackPages(runStart, f);
            inRun = 0;
        }
    }
    if (inRun)
        SimBackPages(runStart, hi);
}

void HostInitFrames(HostMachine *m)
{
    HostBackMachine(m);
    MiBootAllocatorInit(&m->db, &g_hw, &m->boot, m->extra, m->extraCount);
    MiFrameDbInit(&m->db);
}

ULONG64 HostCheckFrames(const HostMachine *m)
{
    const MM_FRAME_DB *db          = &m->db;
    ULONG64            diff        = 0;
    ULONG64            usableClass = 0, freeOrBoot = 0;
    ULONG64            dbFirst = db->DbPhysical >> PAGE_SHIFT;
    ULONG64            dbEnd   = dbFirst + db->DbPages;
    for (ULONG64 f = 0; f < db->FrameCount; f++) {
        const MM_PFN *p    = &db->Pfn[f];
        HostExpect    e    = HostClassify(m, f);
        int           inDb = f >= dbFirst && f < dbEnd;
        switch (e) {
        case HostExpectNone:
            diff += p->State != MmFrameNonexistent;
            break;
        case HostExpectBad:
            diff += p->State != MmFrameBad;
            break;
        case HostExpectReserved:
            diff += p->State != MmFrameReserved;
            break;
        case HostExpectAcpi:
            diff += p->State != MmFrameAcpi;
            break;
        case HostExpectUsable:
            usableClass++;
            if (inDb)
                diff += !(p->State == MmFrameReserved && (p->Flags & MM_PFN_FLAG_DATABASE));
            else if (p->State == MmFrameReserved)
                diff += !(p->Flags & MM_PFN_FLAG_BOOT);
            else
                diff += p->State != MmFrameFree && p->State != MmFrameActive &&
                        p->State != MmFramePageTable;
            freeOrBoot++;
            break;
        }
        if (inDb && e != HostExpectUsable)
            diff++;
    }
    /* Nothing usable beyond the database: that memory would be lost. */
    for (ULONG64 f = db->FrameCount; f < db->FrameCount + 64u; f++)
        diff += HostClassify(m, f) == HostExpectUsable;
    (void)usableClass;
    (void)freeOrBoot;
    return diff;
}

/* ------------------------------------------------------------------ whole memory manager */

long g_tableFailAfter = -1; /* page-table frame allocations that still succeed; -1 = never fail */
unsigned g_tableAllocs;     /* page-table frame allocations attempted */
long     g_poolFailAfter = -1;
long     g_poolPages;      /* pages handed out by the host page provider and not returned */
int      g_poolQuarantine; /* keep returned pages instead of freeing them */

typedef struct Quarantined {
    void               *va;
    struct Quarantined *next;
} Quarantined;
static Quarantined *g_quarantine;

static NTSTATUS HostTableAllocate(void *Context, PHYSICAL_ADDRESS *Physical)
{
    HostMachine *m = Context;
    g_tableAllocs++;
    if (g_tableFailAfter == 0)
        return STATUS_NO_MEMORY;
    if (g_tableFailAfter > 0)
        g_tableFailAfter--;
    return MiAllocateFrame(&m->db, MmFramePageTable, true, Physical);
}

static void HostTableFree(void *Context, PHYSICAL_ADDRESS Physical)
{
    HostMachine *m = Context;
    (void)MiDereferenceFrame(&m->db, Physical, MmFramePageTable);
}

static NTSTATUS HostGetPages(void *Context, SIZE_T Count, void **Virtual)
{
    (void)Context;
    if (g_poolFailAfter == 0)
        return STATUS_NO_MEMORY;
    if (g_poolFailAfter > 0)
        g_poolFailAfter--;
    void *p = aligned_alloc(PAGE_SIZE, Count * PAGE_SIZE);
    if (p == NULL)
        return STATUS_NO_MEMORY;
    memset(p, 0x5A, Count * PAGE_SIZE); /* Recognisable: the pool must not rely on zeroed pages. */
    g_poolPages += (long)Count;
    *Virtual = p;
    return STATUS_SUCCESS;
}

static void HostPutPages(void *Context, void *Virtual, SIZE_T Count)
{
    (void)Context;
    g_poolPages -= (long)Count;
    if (g_poolQuarantine) {
        Quarantined *q = malloc(sizeof *q);
        if (q == NULL)
            abort();
        q->va        = Virtual;
        q->next      = g_quarantine;
        g_quarantine = q;
        return;
    }
    free(Virtual);
}

void HostInitMm(HostMachine *m, int debugPool)
{
    g_tableFailAfter = -1;
    g_tableAllocs    = 0;
    g_poolFailAfter  = -1;
    g_poolPages      = 0;
    g_poolQuarantine = 0;
    HostStopClear();
    HostInitFrames(m);
    MiContextInit(&m->mm, &g_hw, &m->db, 40);
    m->mm.TableFrames.Context  = m;
    m->mm.TableFrames.Allocate = HostTableAllocate;
    m->mm.TableFrames.Free     = HostTableFree;
    MM_PAGE_PROVIDER prov      = {NULL, HostGetPages, HostPutPages};
    MiPoolInit(&m->mm.Pool, &g_hw, &prov, debugPool != 0);
    if (!NT_SUCCESS(MiInitKernelSpace(&m->mm, 0)))
        abort();
    MiSetGlobalContext(&m->mm);
}

void HostTeardown(HostMachine *m)
{
    m->mm.Pool.Live = 0; /* Tests check the live count themselves; here every page goes back. */
    MiPoolReleaseAll(&m->mm.Pool);
    while (g_quarantine != NULL) {
        Quarantined *q = g_quarantine;
        g_quarantine   = q->next;
        free(q->va);
        free(q);
    }
    SimReset();
}

/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Boot allocator and frame database on the host. Owner: A04.
 * Oracle: a page-by-page classification of the boot map written in the test (HostClassify),
 * a shadow model of the allocator, the simulated physical memory that refuses access to memory
 * the machine does not have, and the lock-order checker.
 */
#define KTEST_HOST 1
#include <ktest.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mm_host.h"

static HostMachine M;
static char        g_msg[160];

#define FAILF(...)                                                                                 \
    do {                                                                                           \
        snprintf(g_msg, sizeof g_msg, __VA_ARGS__);                                                \
        KT_FAIL(g_msg);                                                                            \
    } while (0)

static void finish_checks(void)
{
    KT_CHECK_EQ(g_locks.violations, 0);
    KT_CHECK_EQ(g_locks.depth, 0);
    KT_CHECK_EQ(g_simWild, 0);
}

static ULONG64 count_expect(const HostMachine *m, HostExpect e)
{
    ULONG64 n = 0;
    for (ULONG64 f = 0; f < m->db.FrameCount; f++)
        n += HostClassify(m, f) == e;
    return n;
}

static void test_standard_map(void)
{
    HostMachineStandard(&M);
    HostInitFrames(&M);
    KT_CHECK_EQ(HostCheckFrames(&M), 0);
    KT_CHECK_EQ(MiFrameDbVerify(&M.db), 0);
    ULONG64 usable = count_expect(&M, HostExpectUsable);
    KT_CHECK_EQ(M.db.StateCount[MmFrameFree], usable - M.db.DbPages);
    KT_CHECK_EQ(M.db.StateCount[MmFrameAcpi], count_expect(&M, HostExpectAcpi));
    KT_CHECK_EQ(M.db.StateCount[MmFrameReserved],
                count_expect(&M, HostExpectReserved) + M.db.DbPages);
    KT_CHECK_EQ(M.db.StateCount[MmFrameBad], 0);
    KT_CHECK(M.db.StateCount[MmFrameFree] > 8000);
    /* The first MiB, the kernel image, the module and the loader structure are not free. */
    KT_CHECK_EQ(MiGetFrame(&M.db, 0)->State, MmFrameReserved);
    KT_CHECK_EQ(MiGetFrame(&M.db, 0x200000)->State, MmFrameReserved);
    KT_CHECK_EQ(MiGetFrame(&M.db, 0x400000 + 0x10000)->State, MmFrameReserved); /* last, partial */
    KT_CHECK_EQ(MiGetFrame(&M.db, 0x1801000)->State, MmFrameReserved);
    KT_CHECK_EQ(MiGetFrame(&M.db, 0x1000000)->State, MmFrameAcpi);
    KT_CHECK_EQ(MiGetFrame(&M.db, 0x1010000)->State, MmFrameReserved); /* NVS */
    KT_CHECK(MiGetFrame(&M.db, 0x300000)->State == MmFrameFree);
    KT_CHECK(MiGetFrame(&M.db, 0x2800000) == NULL); /* beyond the database */
    finish_checks();
    HostTeardown(&M);
}

typedef void (*map_builder)(HostMachine *m);

static void map_unaligned(HostMachine *m)
{
    memset(m, 0, sizeof *m);
    HostAddRegion(m, 0x100800, 0x2FF900, BootMemoryUsable); /* ends 0x400100 */
    HostAddRegion(m, 0x400100, 0x1000, BootMemoryReserved);
    HostAddRegion(m, 0x401100, 0x7FEF00, BootMemoryUsable); /* ends 0xC00000 */
    HostAddRegion(m, 0xC00000, 0x100, BootMemoryReserved);
}

static void map_tiny_pieces(HostMachine *m)
{
    memset(m, 0, sizeof *m);
    HostAddRegion(m, 0x100000, 0x400000, BootMemoryUsable);
    HostAddRegion(m, 0x800000, 0x1000, BootMemoryUsable); /* one page */
    HostAddRegion(m, 0x900000, 0xFFF, BootMemoryUsable);  /* less than a page */
    HostAddRegion(m, 0xA00000, 0x2000, BootMemoryBad);
    HostAddRegion(m, 0xB00800, 0x1000, BootMemoryUsable); /* straddles two pages */
}

static void map_shared_page(HostMachine *m)
{
    memset(m, 0, sizeof *m);
    HostAddRegion(m, 0x100000, 0x200800, BootMemoryUsable); /* ends 0x300800 */
    HostAddRegion(m, 0x300800, 0x200000, BootMemoryUsable); /* the page 0x300000 is in neither */
    HostAddRegion(m, 0x500800, 0x100, BootMemoryReserved);
    HostAddRegion(m, 0x500900, 0x100, BootMemoryBad); /* same page: bad wins */
    HostAddRegion(m, 0x500A00, 0x1000, BootMemoryAcpiReclaimable);
    HostAddRegion(m, 0x501A00, 0x400000, BootMemoryUsable);
}

static void map_many_regions(HostMachine *m)
{
    memset(m, 0, sizeof *m);
    for (unsigned i = 0; i < BOOT_MAX_MEMORY_REGIONS; i++)
        HostAddRegion(m, 0x100000 + (PHYSICAL_ADDRESS)i * 0x8000, 0x7000, BootMemoryUsable);
}

static void map_sparse_high(HostMachine *m)
{
    memset(m, 0, sizeof *m);
    HostAddRegion(m, 0x100000, 0x1000000, BootMemoryUsable);
    HostAddRegion(m, 0x40000000, 0x100000, BootMemoryUsable); /* 1 GiB: a long hole below */
}

static void map_reservations_inside(HostMachine *m)
{
    memset(m, 0, sizeof *m);
    HostAddRegion(m, 0x100000, 0x1000000, BootMemoryUsable);
    m->boot.ModuleCount = BOOT_MAX_MODULES;
    for (unsigned i = 0; i < BOOT_MAX_MODULES; i++) {
        m->boot.Modules[i].Base   = 0x200000 + (PHYSICAL_ADDRESS)i * 0x40000 + 0x123;
        m->boot.Modules[i].Length = 0x1000 * (i + 1u);
    }
    m->boot.Framebuffer.Present = true;
    m->boot.Framebuffer.Base    = 0xF00000;
    m->boot.Framebuffer.Pitch   = 4096;
    m->boot.Framebuffer.Height  = 3;
    m->boot.AcpiRsdp            = 0xA00123;
    m->extraCount               = MM_MAX_EXTRA_RESERVATIONS;
    for (unsigned i = 0; i < MM_MAX_EXTRA_RESERVATIONS; i++) {
        m->extra[i].Base   = 0x700000 + (PHYSICAL_ADDRESS)i * 0x10000 + 0x7FF;
        m->extra[i].Length = 0x1001;
    }
}

static void test_hostile_maps(void)
{
    static const map_builder builders[] = {
        map_unaligned,    map_tiny_pieces, map_shared_page,
        map_many_regions, map_sparse_high, map_reservations_inside,
    };
    for (unsigned i = 0; i < KAMOS_ARRAY_SIZE(builders); i++) {
        builders[i](&M);
        HostInitFrames(&M);
        ULONG64 diff = HostCheckFrames(&M);
        ULONG64 bad  = MiFrameDbVerify(&M.db);
        if (diff != 0 || bad != 0)
            FAILF("map %u: %lu frames differ from the classification, %lu database defects", i,
                  (unsigned long)diff, (unsigned long)bad);
        ULONG64 usable = count_expect(&M, HostExpectUsable);
        if (M.db.StateCount[MmFrameFree] != usable - M.db.DbPages)
            FAILF("map %u: free %lu, expected %lu", i, (unsigned long)M.db.StateCount[MmFrameFree],
                  (unsigned long)(usable - M.db.DbPages));
        /* Hand out every frame: none may be a frame the classification refuses. */
        PHYSICAL_ADDRESS pa;
        ULONG64          n = 0;
        while (MiAllocateFrame(&M.db, MmFrameActive, false, &pa) == STATUS_SUCCESS) {
            if (HostClassify(&M, pa >> PAGE_SHIFT) != HostExpectUsable)
                FAILF("map %u: frame %#lx handed out but not usable", i, (unsigned long)pa);
            n++;
        }
        if (n != M.db.StateCount[MmFrameActive] || M.db.StateCount[MmFrameFree] != 0)
            FAILF("map %u: handed out %lu frames, counted %lu", i, (unsigned long)n,
                  (unsigned long)M.db.StateCount[MmFrameActive]);
        if (g_simWild != 0 || g_locks.violations != 0)
            FAILF("map %u: wild access or lock violation", i);
        HostTeardown(&M);
    }
    finish_checks();
}

/* A valid base map whose copy is spoiled by the case. */
static void valid_base(HostMachine *m)
{
    memset(m, 0, sizeof *m);
    HostAddRegion(m, 0x0, 0x9F000, BootMemoryUsable);
    HostAddRegion(m, 0x9F000, 0x61000, BootMemoryReserved);
    HostAddRegion(m, 0x100000, 0x400000, BootMemoryUsable);
}

static int boot_stop_reason(HostMachine *m, int *code, unsigned long *reason)
{
    volatile int stopped = 0;
    HOST_CATCH(stopped, MiBootAllocatorInit(&m->db, &g_hw, &m->boot, m->extra, m->extraCount));
    *code   = g_stop.code;
    *reason = (unsigned long)g_stop.p[0];
    return stopped;
}

static void test_malformed_maps_stop(void)
{
    int           code;
    unsigned long reason;
    int           stopped;

    valid_base(&M);
    stopped = boot_stop_reason(&M, &code, &reason);
    KT_REQUIRE_EQ(stopped, 0); /* control: the unspoiled map is accepted */

    /* Each spoiled copy must stop the kernel with KAMOS_STOP_BAD_BOOT_INFORMATION and the reason.
     */
#define CASE(label, expect, ...)                                                                   \
    do {                                                                                           \
        valid_base(&M);                                                                            \
        __VA_ARGS__;                                                                               \
        stopped = boot_stop_reason(&M, &code, &reason);                                            \
        if (!stopped)                                                                              \
            FAILF("%s: accepted", label);                                                          \
        if (code != KAMOS_STOP_BAD_BOOT_INFORMATION || reason != (expect))                         \
            FAILF("%s: stop code %d reason %#lx, expected reason %#x", label, code, reason,        \
                  (unsigned)(expect));                                                             \
        if (g_stop.lockDepthAtStop != 0)                                                           \
            FAILF("%s: stopped with a lock held", label);                                          \
    } while (0)

    CASE("unsorted", MM_BOOT_NOT_SORTED, {
        BOOT_MEMORY_REGION t = M.boot.Regions[1];
        M.boot.Regions[1]    = M.boot.Regions[2];
        M.boot.Regions[2]    = t;
    });
    CASE("overlap", MM_BOOT_OVERLAP, M.boot.Regions[2].Base = 0xA0000);
    CASE("overlap by one byte", MM_BOOT_OVERLAP, M.boot.Regions[1].Length = 0x61001);
    CASE("same base", MM_BOOT_OVERLAP, { M.boot.Regions[2].Base = M.boot.Regions[1].Base; });
    CASE("type 0", MM_BOOT_BAD_REGION_TYPE, M.boot.Regions[1].Type = (BOOT_MEMORY_TYPE)0);
    CASE("type 6", MM_BOOT_BAD_REGION_TYPE, M.boot.Regions[1].Type = (BOOT_MEMORY_TYPE)6);
    CASE("length wraps", MM_BOOT_REGION_OVERFLOW, {
        M.boot.Regions[2].Base   = ~(ULONG64)0xFFF;
        M.boot.Regions[2].Length = 0x2000;
    });
    CASE("beyond 2^52", MM_BOOT_REGION_OVERFLOW, {
        M.boot.Regions[2].Base   = UINT64_C(1) << 52;
        M.boot.Regions[2].Length = 0x1000;
    });
    CASE("no regions", MM_BOOT_BAD_REGION_COUNT, M.boot.RegionCount = 0);
    CASE("129 regions", MM_BOOT_BAD_REGION_COUNT, M.boot.RegionCount = BOOT_MAX_MEMORY_REGIONS + 1);
    CASE("nothing usable", MM_BOOT_NO_USABLE_MEMORY, {
        M.boot.Regions[0].Type = BootMemoryReserved;
        M.boot.Regions[2].Type = BootMemoryBad;
    });
    CASE("only the first MiB", MM_BOOT_NO_USABLE_MEMORY, M.boot.RegionCount = 1);
    CASE("9 modules", MM_BOOT_BAD_RESERVATION, M.boot.ModuleCount = BOOT_MAX_MODULES + 1);
    CASE("9 extra ranges", MM_BOOT_BAD_RESERVATION, M.extraCount = MM_MAX_EXTRA_RESERVATIONS + 1);
    CASE("module wraps", MM_BOOT_BAD_RESERVATION, {
        M.boot.ModuleCount       = 1;
        M.boot.Modules[0].Base   = ~(ULONG64)0;
        M.boot.Modules[0].Length = 2;
    });
    CASE("reservation covers all usable memory", MM_BOOT_NO_USABLE_MEMORY, {
        M.extraCount      = 1;
        M.extra[0].Base   = 0x100000;
        M.extra[0].Length = 0x400000;
    });
#undef CASE

    /* Tolerated: a zero-length entry. */
    valid_base(&M);
    M.boot.Regions[1].Length = 0;
    stopped                  = boot_stop_reason(&M, &code, &reason);
    KT_CHECK_EQ(stopped, 0);

    /* The frame database does not fit: stop with MM_BOOT_DB_NO_ROOM, before touching memory. */
    memset(&M, 0, sizeof M);
    HostAddRegion(&M, 0x4000000, 0x1000, BootMemoryUsable); /* one page; the database needs 33 */
    HostBackMachine(&M);
    MiBootAllocatorInit(&M.db, &g_hw, &M.boot, M.extra, M.extraCount);
    HOST_CATCH(stopped, MiFrameDbInit(&M.db));
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.code, KAMOS_STOP_BAD_BOOT_INFORMATION);
    KT_CHECK_EQ(g_stop.p[0], MM_BOOT_DB_NO_ROOM);
    finish_checks();
    HostTeardown(&M);
}

static void test_alloc_unique_until_exhausted(void)
{
    HostMachineSmall(&M, 3);
    HostInitFrames(&M);
    ULONG64           free0  = M.db.StateCount[MmFrameFree];
    ULONG64           frames = M.db.FrameCount;
    uint8_t          *seen   = calloc(frames, 1);
    PHYSICAL_ADDRESS *got    = calloc(frames, sizeof *got);
    KT_REQUIRE(seen != NULL && got != NULL);

    PHYSICAL_ADDRESS pa;
    ULONG64          n = 0;
    while (MiAllocateFrame(&M.db, MmFrameActive, false, &pa) == STATUS_SUCCESS) {
        ULONG64 f = pa >> PAGE_SHIFT;
        if ((pa & 0xFFF) != 0 || f >= frames || seen[f] ||
            HostClassify(&M, f) != HostExpectUsable ||
            (f >= (M.db.DbPhysical >> PAGE_SHIFT) &&
             f < (M.db.DbPhysical >> PAGE_SHIFT) + M.db.DbPages)) {
            free(seen);
            free(got);
            FAILF("allocation %lu returned %#lx (duplicate, unaligned, not usable or the database)",
                  (unsigned long)n, (unsigned long)pa);
        }
        seen[f]  = 1;
        got[n++] = pa;
    }
    KT_CHECK_EQ(n, free0);
    KT_CHECK_EQ(M.db.StateCount[MmFrameFree], 0);
    KT_CHECK_EQ(M.db.StateCount[MmFrameActive], free0);
    KT_CHECK_EQ(MiAllocateFrame(&M.db, MmFrameActive, false, &pa), STATUS_NO_MEMORY);
    KT_CHECK_EQ(MiFrameDbVerify(&M.db), 0);

    /* Free one frame in the middle: it is the only one the next allocation can return. */
    PHYSICAL_ADDRESS mid = got[n / 2];
    KT_CHECK_EQ(MiDereferenceFrame(&M.db, mid, MmFrameActive), 0);
    KT_CHECK_EQ(MiAllocateFrame(&M.db, MmFrameActive, true, &pa), STATUS_SUCCESS);
    KT_CHECK_EQ(pa, mid);

    for (ULONG64 i = 0; i < n; i++)
        (void)MiDereferenceFrame(&M.db, got[i], MmFrameActive);
    KT_CHECK_EQ(M.db.StateCount[MmFrameFree], free0);
    KT_CHECK_EQ(M.db.StateCount[MmFrameActive], 0);
    KT_CHECK_EQ(MiFrameDbVerify(&M.db), 0);
    KT_CHECK_EQ(HostCheckFrames(&M), 0);
    free(seen);
    free(got);
    finish_checks();
    HostTeardown(&M);
}

static void test_zeroing(void)
{
    HostMachineSmall(&M, 2);
    HostInitFrames(&M);
    PHYSICAL_ADDRESS pa;
    PHYSICAL_ADDRESS list[1024];
    unsigned         n = 0;
    while (n < 1024 && MiAllocateFrame(&M.db, MmFrameActive, false, &pa) == STATUS_SUCCESS) {
        memset(g_hw.PhysicalToPointer(NULL, pa, PAGE_SIZE), 0xAB, PAGE_SIZE);
        list[n++] = pa;
    }
    KT_REQUIRE(n > 100);
    for (unsigned i = 0; i < n; i++)
        (void)MiDereferenceFrame(&M.db, list[i], MmFrameActive);
    for (unsigned i = 0; i < n; i++) {
        KT_REQUIRE_EQ(MiAllocateFrame(&M.db, MmFrameActive, true, &pa), STATUS_SUCCESS);
        const uint8_t *p  = g_hw.PhysicalToPointer(NULL, pa, PAGE_SIZE);
        unsigned       nz = 0;
        for (unsigned b = 0; b < PAGE_SIZE; b++)
            nz += p[b] != 0;
        if (nz != 0)
            FAILF("frame %#lx not zeroed: %u non-zero bytes", (unsigned long)pa, nz);
    }
    finish_checks();
    HostTeardown(&M);
}

typedef struct Snap {
    MM_PFN  *pfn;
    ULONG64 *bm;
    ULONG64  counts[MmFrameStateCount];
    ULONG64  frames, words;
} Snap;

static void snap_take(const MM_FRAME_DB *db, Snap *s)
{
    s->frames = db->FrameCount;
    s->words  = db->BitmapWords;
    s->pfn    = malloc(db->FrameCount * sizeof(MM_PFN));
    s->bm     = malloc(db->BitmapWords * 8);
    memcpy(s->pfn, db->Pfn, db->FrameCount * sizeof(MM_PFN));
    memcpy(s->bm, db->Bitmap, db->BitmapWords * 8);
    memcpy(s->counts, db->StateCount, sizeof s->counts);
}

static int snap_same(const MM_FRAME_DB *db, const Snap *s)
{
    return memcmp(s->pfn, db->Pfn, s->frames * sizeof(MM_PFN)) == 0 &&
           memcmp(s->bm, db->Bitmap, s->words * 8) == 0 &&
           memcmp(s->counts, db->StateCount, sizeof s->counts) == 0;
}

static void snap_free(Snap *s)
{
    free(s->pfn);
    free(s->bm);
}

static void test_invalid_frame_operations_stop(void)
{
    HostMachineStandard(&M);
    HostInitFrames(&M);
    PHYSICAL_ADDRESS a, p;
    KT_REQUIRE_EQ(MiAllocateFrame(&M.db, MmFrameActive, false, &a), STATUS_SUCCESS);
    KT_REQUIRE_EQ(MiAllocateFrame(&M.db, MmFramePageTable, false, &p), STATUS_SUCCESS);
    PHYSICAL_ADDRESS freed;
    KT_REQUIRE_EQ(MiAllocateFrame(&M.db, MmFrameActive, false, &freed), STATUS_SUCCESS);
    KT_REQUIRE_EQ(MiDereferenceFrame(&M.db, freed, MmFrameActive), 0);
    PHYSICAL_ADDRESS beyond = M.db.FrameCount << PAGE_SHIFT;

    Snap before;
    snap_take(&M.db, &before);
    int stopped;

#define STOPS(label, reason, stmt)                                                                 \
    do {                                                                                           \
        HOST_CATCH(stopped, stmt);                                                                 \
        if (!stopped) {                                                                            \
            snap_free(&before);                                                                    \
            FAILF("%s: did not stop", label);                                                      \
        }                                                                                          \
        if (g_stop.p[0] != (MM_STOP_BASE | (reason))) {                                            \
            snap_free(&before);                                                                    \
            FAILF("%s: reason %#lx, expected %#x", label, (unsigned long)g_stop.p[0],              \
                  (unsigned)(MM_STOP_BASE | (reason)));                                            \
        }                                                                                          \
        if (g_stop.lockDepthAtStop != 0 || !snap_same(&M.db, &before)) {                           \
            snap_free(&before);                                                                    \
            FAILF("%s: lock held at the stop or the database changed", label);                     \
        }                                                                                          \
    } while (0)

    STOPS("double free", MM_STOP_FRAME_DOUBLE_FREE,
          MiDereferenceFrame(&M.db, freed, MmFrameActive));
    STOPS("free a reserved frame", MM_STOP_FRAME_BAD_STATE,
          MiDereferenceFrame(&M.db, 0x200000, MmFrameActive));
    STOPS("free an ACPI frame", MM_STOP_FRAME_BAD_STATE,
          MiDereferenceFrame(&M.db, 0x1000000, MmFrameActive));
    STOPS("free the database", MM_STOP_FRAME_BAD_STATE,
          MiDereferenceFrame(&M.db, M.db.DbPhysical, MmFrameActive));
    STOPS("unaligned", MM_STOP_FRAME_BAD_ADDRESS, MiDereferenceFrame(&M.db, a + 1, MmFrameActive));
    STOPS("beyond the database", MM_STOP_FRAME_BAD_ADDRESS,
          MiDereferenceFrame(&M.db, beyond, MmFrameActive));
    STOPS("huge address", MM_STOP_FRAME_BAD_ADDRESS,
          MiDereferenceFrame(&M.db, ~(PHYSICAL_ADDRESS)0xFFF, MmFrameActive));
    STOPS("active freed as page table", MM_STOP_FRAME_BAD_STATE,
          MiDereferenceFrame(&M.db, a, MmFramePageTable));
    STOPS("page table freed as active", MM_STOP_FRAME_BAD_STATE,
          MiDereferenceFrame(&M.db, p, MmFrameActive));
    STOPS("reference a free frame", MM_STOP_FRAME_BAD_STATE, MiReferenceFrame(&M.db, freed));
    STOPS("reference a page table", MM_STOP_FRAME_BAD_STATE, MiReferenceFrame(&M.db, p));
    STOPS("reference beyond", MM_STOP_FRAME_BAD_ADDRESS, MiReferenceFrame(&M.db, beyond));
    STOPS("allocate as free", MM_STOP_FRAME_BAD_STATE,
          MiAllocateFrame(&M.db, MmFrameFree, false, &freed));

    /* Reference count overflow: poke the count to its maximum, as a corrupt database would. */
    M.db.Pfn[a >> PAGE_SHIFT].RefCount = 0xFFFFFFFFu;
    snap_free(&before);
    snap_take(&M.db, &before);
    STOPS("reference overflow", MM_STOP_FRAME_REFCOUNT, MiReferenceFrame(&M.db, a));
    M.db.Pfn[a >> PAGE_SHIFT].RefCount = 1;
#undef STOPS
    snap_free(&before);

    /* After all that the database is still consistent and the good frames still free correctly. */
    KT_CHECK_EQ(MiFrameDbVerify(&M.db), 0);
    KT_CHECK_EQ(MiDereferenceFrame(&M.db, a, MmFrameActive), 0);
    KT_CHECK_EQ(MiDereferenceFrame(&M.db, p, MmFramePageTable), 0);
    finish_checks();
    HostTeardown(&M);
}

static void test_reference_counting(void)
{
    HostMachineSmall(&M, 2);
    HostInitFrames(&M);
    PHYSICAL_ADDRESS a;
    KT_REQUIRE_EQ(MiAllocateFrame(&M.db, MmFrameActive, false, &a), STATUS_SUCCESS);
    ULONG64 freeBefore = M.db.StateCount[MmFrameFree];
    MiReferenceFrame(&M.db, a);
    MiReferenceFrame(&M.db, a);
    KT_CHECK_EQ(MiGetFrame(&M.db, a)->RefCount, 3);
    KT_CHECK_EQ(MiDereferenceFrame(&M.db, a, MmFrameActive), 2);
    KT_CHECK_EQ(MiDereferenceFrame(&M.db, a, MmFrameActive), 1);
    KT_CHECK_EQ(MiGetFrame(&M.db, a)->State, MmFrameActive);
    KT_CHECK_EQ(M.db.StateCount[MmFrameFree], freeBefore);
    KT_CHECK_EQ(MiDereferenceFrame(&M.db, a, MmFrameActive), 0);
    KT_CHECK_EQ(MiGetFrame(&M.db, a)->State, MmFrameFree);
    KT_CHECK_EQ(M.db.StateCount[MmFrameFree], freeBefore + 1);
    KT_CHECK_EQ(MiFrameDbVerify(&M.db), 0);
    finish_checks();
    HostTeardown(&M);
}

static void test_boot_allocator(void)
{
    HostMachineStandard(&M);
    HostBackMachine(&M);
    MiBootAllocatorInit(&M.db, &g_hw, &M.boot, M.extra, M.extraCount);
    PHYSICAL_ADDRESS     got[3];
    static const ULONG64 sizes[3] = {1, 4, 16};
    for (unsigned i = 0; i < 3; i++) {
        KT_REQUIRE_EQ(MiBootAllocatePages(&M.db, sizes[i], &got[i]), STATUS_SUCCESS);
        for (ULONG64 k = 0; k < sizes[i]; k++)
            if (HostClassify(&M, (got[i] >> PAGE_SHIFT) + k) != HostExpectUsable)
                FAILF("boot allocation %u: page %lu is not usable", i, (unsigned long)k);
    }
    for (unsigned i = 0; i < 3; i++)
        for (unsigned j = i + 1; j < 3; j++)
            KT_CHECK(got[i] + (sizes[i] << PAGE_SHIFT) <= got[j] ||
                     got[j] + (sizes[j] << PAGE_SHIFT) <= got[i]);
    PHYSICAL_ADDRESS x;
    KT_CHECK_EQ(MiBootAllocatePages(&M.db, 0, &x), STATUS_INVALID_PARAMETER);
    KT_CHECK_EQ(MiBootAllocatePages(&M.db, UINT64_C(1) << 30, &x), STATUS_NO_MEMORY);

    MiFrameDbInit(&M.db);
    for (unsigned i = 0; i < 3; i++)
        for (ULONG64 k = 0; k < sizes[i]; k++) {
            const MM_PFN *p = MiGetFrame(&M.db, got[i] + (k << PAGE_SHIFT));
            if (p->State != MmFrameReserved || !(p->Flags & MM_PFN_FLAG_BOOT))
                FAILF("boot page %u/%lu is not reserved after the database exists", i,
                      (unsigned long)k);
        }
    KT_CHECK_EQ(HostCheckFrames(&M), 0);
    KT_CHECK_EQ(MiFrameDbVerify(&M.db), 0);
    int stopped;
    HOST_CATCH(stopped, (void)MiBootAllocatePages(&M.db, 1, &x));
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.p[0], MM_STOP_BASE | MM_STOP_BOOT_ALLOC_CLOSED);
    HOST_CATCH(stopped, MiFrameDbInit(&M.db));
    KT_CHECK_EQ(stopped, 1); /* initialising twice */
    finish_checks();
    HostTeardown(&M);
}

static uint8_t  g_shadowState[1u << 16];
static uint32_t g_shadowRef[1u << 16];

static void test_random_sequences(void)
{
    static const uint64_t seeds[] = {1,   2,   3,   5,   8,   13,   21,   34,   55,   89,
                                     144, 233, 377, 610, 987, 1597, 2584, 4181, 6765, 10946};
    for (unsigned si = 0; si < KAMOS_ARRAY_SIZE(seeds); si++) {
        HostRng rng = {seeds[si] * UINT64_C(0x9E3779B97F4A7C15)};
        HostMachineSmall(&M, 6);
        HostInitFrames(&M);
        ULONG64 frames = M.db.FrameCount;
        KT_REQUIRE(frames <= (1u << 16));
        memset(g_shadowState, 0, sizeof g_shadowState);
        memset(g_shadowRef, 0, sizeof g_shadowRef);
        ULONG64                 shadowFree = M.db.StateCount[MmFrameFree];
        static PHYSICAL_ADDRESS live[1u << 16];
        unsigned                nlive = 0;

        for (unsigned op = 0; op < 20000; op++) {
            unsigned r = (unsigned)HostRandBelow(&rng, 100);
            if (r < 40) {
                MM_FRAME_STATE   st   = (HostRand(&rng) & 1) ? MmFrameActive : MmFramePageTable;
                BOOLEAN          zero = (HostRand(&rng) & 1) != 0;
                PHYSICAL_ADDRESS pa;
                NTSTATUS         s = MiAllocateFrame(&M.db, st, zero, &pa);
                if (shadowFree == 0) {
                    if (s != STATUS_NO_MEMORY)
                        FAILF("seed %lu op %u: allocation succeeded with no free frame",
                              (unsigned long)seeds[si], op);
                } else {
                    if (s != STATUS_SUCCESS)
                        FAILF("seed %lu op %u: allocation failed with %lu free",
                              (unsigned long)seeds[si], op, (unsigned long)shadowFree);
                    ULONG64 f = pa >> PAGE_SHIFT;
                    if (g_shadowRef[f] != 0 || HostClassify(&M, f) != HostExpectUsable)
                        FAILF("seed %lu op %u: frame %#lx already live or not usable",
                              (unsigned long)seeds[si], op, (unsigned long)pa);
                    if (zero) {
                        const uint64_t *w = g_hw.PhysicalToPointer(NULL, pa, PAGE_SIZE);
                        for (unsigned i = 0; i < PAGE_SIZE / 8; i++)
                            if (w[i] != 0)
                                FAILF("seed %lu op %u: frame %#lx not zero",
                                      (unsigned long)seeds[si], op, (unsigned long)pa);
                    }
                    ((uint64_t *)g_hw.PhysicalToPointer(NULL, pa, PAGE_SIZE))[7] = pa ^ 0x55;
                    g_shadowRef[f]                                               = 1;
                    g_shadowState[f]                                             = (uint8_t)st;
                    live[nlive++]                                                = pa;
                    shadowFree--;
                }
            } else if (r < 80 && nlive > 0) {
                unsigned         k  = (unsigned)HostRandBelow(&rng, nlive);
                PHYSICAL_ADDRESS pa = live[k];
                ULONG64          f  = pa >> PAGE_SHIFT;
                if (*((uint64_t *)g_hw.PhysicalToPointer(NULL, pa, PAGE_SIZE) + 7) != (pa ^ 0x55))
                    FAILF("seed %lu op %u: frame %#lx content changed while owned",
                          (unsigned long)seeds[si], op, (unsigned long)pa);
                ULONG left = MiDereferenceFrame(&M.db, pa, (MM_FRAME_STATE)g_shadowState[f]);
                g_shadowRef[f]--;
                if (left != g_shadowRef[f])
                    FAILF("seed %lu op %u: refcount %u, expected %u", (unsigned long)seeds[si], op,
                          left, g_shadowRef[f]);
                if (left == 0) {
                    live[k] = live[--nlive];
                    shadowFree++;
                }
            } else if (nlive > 0) {
                unsigned         k  = (unsigned)HostRandBelow(&rng, nlive);
                PHYSICAL_ADDRESS pa = live[k];
                ULONG64          f  = pa >> PAGE_SHIFT;
                if (g_shadowState[f] == MmFrameActive) {
                    MiReferenceFrame(&M.db, pa);
                    g_shadowRef[f]++;
                }
            }
            if (op % 1000 == 999) {
                if (MiFrameDbVerify(&M.db) != 0 || M.db.StateCount[MmFrameFree] != shadowFree)
                    FAILF("seed %lu op %u: database inconsistent or free count %lu != %lu",
                          (unsigned long)seeds[si], op, (unsigned long)M.db.StateCount[MmFrameFree],
                          (unsigned long)shadowFree);
            }
        }
        for (unsigned k = 0; k < nlive; k++) {
            ULONG64 f = live[k] >> PAGE_SHIFT;
            while (g_shadowRef[f] > 0) {
                (void)MiDereferenceFrame(&M.db, live[k], (MM_FRAME_STATE)g_shadowState[f]);
                g_shadowRef[f]--;
            }
        }
        if (M.db.StateCount[MmFrameActive] != 0 || M.db.StateCount[MmFramePageTable] != 0 ||
            HostCheckFrames(&M) != 0 || MiFrameDbVerify(&M.db) != 0)
            FAILF("seed %lu: final state differs from the classification",
                  (unsigned long)seeds[si]);
        if (g_locks.violations != 0 || g_locks.depth != 0 || g_simWild != 0)
            FAILF("seed %lu: lock or wild access problem", (unsigned long)seeds[si]);
        HostTeardown(&M);
    }
    finish_checks();
}

static void test_frame_lock_rank(void)
{
    HostMachineSmall(&M, 2);
    HostInitFrames(&M);
    PHYSICAL_ADDRESS a;
    unsigned         before = g_locks.acquired[LOCK_RANK_FRAME_DB];
    KT_REQUIRE_EQ(MiAllocateFrame(&M.db, MmFrameActive, false, &a), STATUS_SUCCESS);
    (void)MiDereferenceFrame(&M.db, a, MmFrameActive);
    KT_CHECK_EQ(g_locks.acquired[LOCK_RANK_FRAME_DB], before + 2);
    KT_CHECK_EQ(g_locks.total, before + 2); /* nothing else was taken */
    finish_checks();
    HostTeardown(&M);
}

int main(void)
{
    static const kt_case cases[] = {
        KT_CASE("mm.frames", test_standard_map),
        KT_CASE("mm.frames", test_hostile_maps),
        KT_CASE("mm.frames", test_malformed_maps_stop),
        KT_CASE("mm.frames", test_alloc_unique_until_exhausted),
        KT_CASE("mm.frames", test_zeroing),
        KT_CASE("mm.frames", test_invalid_frame_operations_stop),
        KT_CASE("mm.frames", test_reference_counting),
        KT_CASE("mm.frames", test_boot_allocator),
        KT_CASE("mm.frames", test_random_sequences),
        KT_CASE("mm.frames", test_frame_lock_rank),
    };
    return kt_run(cases, KAMOS_ARRAY_SIZE(cases)) ? 1 : 0;
}

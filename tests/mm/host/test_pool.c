/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Kernel pool on the host. Owner: A04.
 * Oracles: a shadow table of live allocations (address, size, tag, fill pattern) kept by the test,
 * interval arithmetic for overlap, the size-class table written down independently, the page
 * counter of the host page provider, and the lock checker.
 */
#define KTEST_HOST 1
#include <ktest.h>

#include <stdio.h>
#include <string.h>

#include "mm_host.h"

static HostMachine M;
static char        g_msg[160];

#define FAILF(...)                                                                                 \
    do {                                                                                           \
        snprintf(g_msg, sizeof g_msg, __VA_ARGS__);                                                \
        KT_FAIL(g_msg);                                                                            \
    } while (0)

#define TAG(a, b, c, d) ((ULONG)(a) | ((ULONG)(b) << 8) | ((ULONG)(c) << 16) | ((ULONG)(d) << 24))

static void setup(int debug)
{
    HostMachineSmall(&M, 4);
    HostInitMm(&M, debug);
}

static void finish_checks(void)
{
    KT_CHECK_EQ(g_locks.violations, 0);
    KT_CHECK_EQ(g_locks.depth, 0);
    KT_CHECK_EQ(g_providerLockDepth, 0); /* no lock is held while the pool asks for pages */
    KT_CHECK_EQ(g_simWild, 0);
}

/* Class table written from the design: block sizes 64 to 1024, 32-byte header, 8-byte trailer. */
static unsigned class_size_for(size_t size)
{
    static const unsigned blocks[] = {64, 128, 256, 512, 1024};
    for (unsigned i = 0; i < 5; i++)
        if (size + 40 <= blocks[i])
            return blocks[i];
    return 0; /* large */
}

static size_t large_pages_for(size_t size)
{
    return (32 + size + 8 + 4095) / 4096;
}

static void test_sizes_alignment_and_classes(void)
{
    setup(1);
    static const size_t sizes[] = {1,   2,   7,   8,    15,   16,   17,   24,   25,    31,    32,
                                   56,  57,  88,  89,   100,  216,  217,  300,  472,   473,   600,
                                   983, 984, 985, 1000, 2000, 4056, 4057, 8192, 10000, 100000};
    ULONG               tag     = TAG('S', 'i', 'z', 'e');
    for (unsigned i = 0; i < KAMOS_ARRAY_SIZE(sizes); i++) {
        size_t   n = sizes[i];
        uint8_t *a = MiPoolAllocate(&M.mm.Pool, NonPagedPool, n, tag);
        uint8_t *b = MiPoolAllocate(&M.mm.Pool, NonPagedPool, n, tag);
        if (a == NULL || b == NULL)
            FAILF("size %zu: allocation failed", n);
        if (((uintptr_t)a & 15) != 0 || ((uintptr_t)b & 15) != 0)
            FAILF("size %zu: not 16-byte aligned", n);
        unsigned cls = class_size_for(n);
        if (cls != 0) {
            /* In a size class the header sits at a multiple of the block size inside its page. */
            if ((((uintptr_t)a - 32) % cls) != 0 || (((uintptr_t)b - 32) % cls) != 0 ||
                ((uintptr_t)a - 32) % 4096 == 0)
                FAILF("size %zu: not at a block boundary of class %u", n, cls);
        } else {
            if (((uintptr_t)a - 32) % 4096 != 0)
                FAILF("size %zu: large block does not start a page", n);
        }
        memset(a, 0x11, n);
        memset(b, 0x22, n);
        if (a[0] != 0x11 || a[n - 1] != 0x11 || b[0] != 0x22 || b[n - 1] != 0x22)
            FAILF("size %zu: blocks overlap", n);
        long pagesBefore = g_poolPages;
        MiPoolFree(&M.mm.Pool, a, tag);
        MiPoolFree(&M.mm.Pool, b, tag);
        if (cls == 0 && g_poolPages != pagesBefore - 2 * (long)large_pages_for(n))
            FAILF("size %zu: large block did not return %zu pages", n, large_pages_for(n));
    }
    KT_CHECK_EQ(M.mm.Pool.Live, 0);
    KT_CHECK_EQ(M.mm.Pool.LargePages, 0);
    MM_POOL_TAG_INFO t;
    KT_REQUIRE(MiPoolFindTag(&M.mm.Pool, tag, &t));
    KT_CHECK_EQ(t.Allocations, 2 * KAMOS_ARRAY_SIZE(sizes));
    KT_CHECK_EQ(t.Frees, t.Allocations);
    KT_CHECK_EQ(t.BytesInUse, 0);
    MiPoolReleaseAll(&M.mm.Pool);
    KT_CHECK_EQ(g_poolPages, 0);
    finish_checks();
    HostTeardown(&M);
}

static void test_refusals(void)
{
    setup(1);
    ULONG tag = TAG('R', 'e', 'f', 'u');
    KT_CHECK(MiPoolAllocate(&M.mm.Pool, NonPagedPool, 0, tag) == NULL);
    KT_CHECK(MiPoolAllocate(&M.mm.Pool, NonPagedPool, (size_t)1 << 41, tag) == NULL);
    KT_CHECK(MiPoolAllocate(&M.mm.Pool, NonPagedPool, SIZE_MAX, tag) == NULL);
    KT_CHECK(MiPoolAllocate(&M.mm.Pool, NonPagedPool, SIZE_MAX - 4096, tag) == NULL);
    KT_CHECK(MiPoolAllocate(&M.mm.Pool, NonPagedPool, 16, 0) == NULL);
    KT_CHECK(MiPoolAllocate(&M.mm.Pool, NonPagedPool, 16, TAG('a', 'b', 'c', 0)) == NULL);
    KT_CHECK(MiPoolAllocate(&M.mm.Pool, NonPagedPool, 16, TAG('a', 'b', 0x7F, 'd')) == NULL);
    KT_CHECK(MiPoolAllocate(&M.mm.Pool, NonPagedPool, 16, TAG('a', 0x1F, 'c', 'd')) == NULL);
    KT_CHECK(MiPoolAllocate(&M.mm.Pool, (POOL_TYPE)2, 16, tag) == NULL);
    KT_CHECK_EQ(M.mm.Pool.Live, 0);
    KT_CHECK_EQ(MiPoolTagCount(&M.mm.Pool), 0);
    KT_CHECK_EQ(g_poolPages, 0);
    MiPoolFree(&M.mm.Pool, NULL, tag); /* freeing NULL is a no-op */
    /* Provider failure: NULL, nothing counted. */
    g_poolFailAfter = 0;
    KT_CHECK(MiPoolAllocate(&M.mm.Pool, NonPagedPool, 16, tag) == NULL);
    KT_CHECK(MiPoolAllocate(&M.mm.Pool, NonPagedPool, 100000, tag) == NULL);
    g_poolFailAfter = -1;
    KT_CHECK_EQ(M.mm.Pool.Live, 0);
    KT_CHECK_EQ(MiPoolTagCount(&M.mm.Pool), 0);
    KT_CHECK_EQ(g_poolPages, 0);
    void *p = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 16, tag);
    KT_CHECK(p != NULL);
    MiPoolFree(&M.mm.Pool, p, tag);
    finish_checks();
    HostTeardown(&M);
}

typedef struct Live {
    uint8_t  *p;
    size_t    size;
    ULONG     tag;
    POOL_TYPE type;
    uint8_t   fill;
} Live;

typedef struct Shadow {
    ULONG   tag;
    ULONG64 allocs, frees, inUse, peak;
} Shadow;

#define MAXLIVE 700u
#define NTAGS   6u

static Live   g_live[MAXLIVE];
static Shadow g_shadow[NTAGS];

static ULONG tag_of(unsigned i)
{
    return TAG('T', 'a', 'g', 'A' + i);
}

static int overlaps_any(const uint8_t *p, size_t size, unsigned n, unsigned skip)
{
    for (unsigned i = 0; i < n; i++) {
        if (i == skip)
            continue;
        if (p < g_live[i].p + g_live[i].size && g_live[i].p < p + size)
            return 1;
    }
    return 0;
}

static void test_random_sequences(void)
{
    static const uint64_t seeds[] = {7, 11, 19, 23, 31, 47, 61, 83, 101, 127, 151, 199};
    for (unsigned si = 0; si < KAMOS_ARRAY_SIZE(seeds); si++) {
        int debug = (si % 3) != 2; /* a third of the runs use the release profile */
        setup(debug);
        HostRng  rng = {seeds[si] * UINT64_C(0x9E3779B97F4A7C15)};
        unsigned n   = 0;
        memset(g_shadow, 0, sizeof g_shadow);
        for (unsigned i = 0; i < NTAGS; i++)
            g_shadow[i].tag = tag_of(i);

        for (unsigned op = 0; op < 4000; op++) {
            unsigned r = (unsigned)HostRandBelow(&rng, 100);
            if ((r < 55 && n < MAXLIVE) || n == 0) {
                size_t size;
                switch (HostRandBelow(&rng, 6)) {
                case 0:
                    size = 1 + HostRandBelow(&rng, 24);
                    break;
                case 1:
                    size = 1 + HostRandBelow(&rng, 200);
                    break;
                case 2:
                    size = 1 + HostRandBelow(&rng, 984);
                    break;
                case 3:
                    size = 985 + HostRandBelow(&rng, 3200);
                    break;
                case 4:
                    size = 4000 + HostRandBelow(&rng, 20000);
                    break;
                default:
                    size = 1 + HostRandBelow(&rng, 100);
                    break;
                }
                unsigned  ti     = (unsigned)HostRandBelow(&rng, NTAGS);
                POOL_TYPE type   = (HostRand(&rng) & 1) ? PagedPool : NonPagedPool;
                int       inject = HostRandBelow(&rng, 50) == 0;
                if (inject)
                    g_poolFailAfter = (long)HostRandBelow(&rng, 2);
                ULONG64  liveBefore = M.mm.Pool.Live;
                uint8_t *p          = MiPoolAllocate(&M.mm.Pool, type, size, tag_of(ti));
                g_poolFailAfter     = -1;
                if (p == NULL) {
                    if (!inject)
                        FAILF("seed %lu op %u: allocation of %zu failed without injection",
                              (unsigned long)seeds[si], op, size);
                    if (M.mm.Pool.Live != liveBefore)
                        FAILF("seed %lu op %u: failed allocation changed the live count",
                              (unsigned long)seeds[si], op);
                    continue;
                }
                if (((uintptr_t)p & 15) != 0 || overlaps_any(p, size, n, n))
                    FAILF("seed %lu op %u: block %p of %zu bytes misaligned or overlapping",
                          (unsigned long)seeds[si], op, (void *)p, size);
                uint8_t fill = (uint8_t)HostRand(&rng);
                memset(p, fill, size);
                g_live[n++] = (Live){p, size, tag_of(ti), type, fill};
                g_shadow[ti].allocs++;
                g_shadow[ti].inUse += size;
                if (g_shadow[ti].inUse > g_shadow[ti].peak)
                    g_shadow[ti].peak = g_shadow[ti].inUse;
            } else if (n > 0) {
                unsigned k = (unsigned)HostRandBelow(&rng, n);
                Live     l = g_live[k];
                for (size_t b = 0; b < l.size; b++)
                    if (l.p[b] != l.fill)
                        FAILF("seed %lu op %u: byte %zu of a live block of %zu changed",
                              (unsigned long)seeds[si], op, b, l.size);
                MiPoolFree(&M.mm.Pool, l.p, l.tag);
                unsigned ti = (l.tag >> 24) - 'A';
                g_shadow[ti].frees++;
                g_shadow[ti].inUse -= l.size;
                g_live[k] = g_live[--n];
            }
            if (op % 250 == 249) {
                if (M.mm.Pool.Live != n)
                    FAILF("seed %lu op %u: live %lu, expected %u", (unsigned long)seeds[si], op,
                          (unsigned long)M.mm.Pool.Live, n);
                for (unsigned i = 0; i < NTAGS; i++) {
                    MM_POOL_TAG_INFO t;
                    if (g_shadow[i].allocs == 0)
                        continue;
                    if (!MiPoolFindTag(&M.mm.Pool, g_shadow[i].tag, &t) ||
                        t.Allocations != g_shadow[i].allocs || t.Frees != g_shadow[i].frees ||
                        t.BytesInUse != g_shadow[i].inUse || t.PeakBytes != g_shadow[i].peak)
                        FAILF("seed %lu op %u: accounting of tag %u differs from the shadow",
                              (unsigned long)seeds[si], op, i);
                }
            }
        }
        while (n > 0) {
            Live l = g_live[--n];
            MiPoolFree(&M.mm.Pool, l.p, l.tag);
        }
        if (M.mm.Pool.Live != 0 || M.mm.Pool.LargePages != 0)
            FAILF("seed %lu: pool not empty at the end", (unsigned long)seeds[si]);
        for (unsigned i = 0; i < NTAGS; i++) {
            MM_POOL_TAG_INFO t;
            if (MiPoolFindTag(&M.mm.Pool, tag_of(i), &t) &&
                (t.BytesInUse != 0 || t.Allocations != t.Frees))
                FAILF("seed %lu: tag %u not balanced", (unsigned long)seeds[si], i);
        }
        MiPoolReleaseAll(&M.mm.Pool);
        if (g_poolPages != 0)
            FAILF("seed %lu: %ld provider pages not returned", (unsigned long)seeds[si],
                  g_poolPages);
        if (g_locks.violations != 0 || g_locks.depth != 0 || g_providerLockDepth != 0)
            FAILF("seed %lu: lock problem", (unsigned long)seeds[si]);
        HostTeardown(&M);
    }
    finish_checks();
}

static void test_tag_table_overflow(void)
{
    setup(1);
    /* 130 distinct tags: the table holds 128, the rest are counted together. */
    void *blocks[130];
    for (unsigned i = 0; i < 130; i++) {
        ULONG tag = TAG('A' + (i / 26), 'a' + (i % 26), 'x', 'y');
        blocks[i] = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 10 + i, tag);
        KT_REQUIRE(blocks[i] != NULL);
    }
    KT_CHECK_EQ(MiPoolTagCount(&M.mm.Pool), 128);
    KT_CHECK_EQ(M.mm.Pool.Other.Allocations, 2);
    KT_CHECK_EQ(M.mm.Pool.Other.BytesInUse, 10 + 128 + 10 + 129);
    for (unsigned i = 0; i < 130; i++) {
        ULONG tag = TAG('A' + (i / 26), 'a' + (i % 26), 'x', 'y');
        MiPoolFree(&M.mm.Pool, blocks[i], tag);
    }
    KT_CHECK_EQ(M.mm.Pool.Other.Frees, 2);
    KT_CHECK_EQ(M.mm.Pool.Other.BytesInUse, 0);
    KT_CHECK_EQ(M.mm.Pool.Live, 0);
    MM_POOL_TAG_INFO t;
    KT_REQUIRE(MiPoolGetTag(&M.mm.Pool, 0, &t));
    KT_CHECK_EQ(t.Tag, TAG('A', 'a', 'x', 'y'));
    KT_CHECK(!MiPoolGetTag(&M.mm.Pool, 128, &t));
    finish_checks();
    HostTeardown(&M);
}

static void test_debug_patterns(void)
{
    setup(1);
    ULONG    tag = TAG('P', 'a', 't', 'n');
    uint8_t *a =
        MiPoolAllocate(&M.mm.Pool, NonPagedPool, 50, tag); /* class 128: 96 bytes of payload */
    KT_REQUIRE(a != NULL);
    for (unsigned i = 0; i < 50; i++)
        KT_CHECK_EQ(a[i], 0xCD); /* uninitialised memory is recognisable */
    memset(a, 0x77, 50);
    MiPoolFree(&M.mm.Pool, a, tag);
    for (unsigned i = 8; i < 96; i++) /* after the free-list link */
        if (a[i] != 0xDD)
            FAILF("byte %u of a freed block is %#x, expected 0xDD", i, a[i]);
    uint8_t *b = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 50, tag);
    KT_CHECK(b == a); /* last in, first out */
    for (unsigned i = 0; i < 50; i++)
        KT_CHECK_EQ(b[i], 0xCD);
    MiPoolFree(&M.mm.Pool, b, tag);

    /* Release profile: no fill, no check, same behaviour otherwise. */
    HostTeardown(&M);
    setup(0);
    a = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 50, tag);
    memset(a, 0x77, 50);
    MiPoolFree(&M.mm.Pool, a, tag);
    KT_CHECK_EQ(a[20], 0x77);
    a[20] = 0x78; /* write after free: not detected in release */
    b     = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 50, tag);
    KT_CHECK(b == a);
    MiPoolFree(&M.mm.Pool, b, tag);
    finish_checks();
    HostTeardown(&M);
}

static void test_types_are_separate(void)
{
    setup(1);
    ULONG tag = TAG('T', 'y', 'p', 'e');
    void *a   = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 50, tag);
    MiPoolFree(&M.mm.Pool, a, tag);
    void *b = MiPoolAllocate(&M.mm.Pool, PagedPool, 50, tag);
    KT_CHECK(a != b); /* a paged request never takes a non-paged block */
    void *c = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 50, tag);
    KT_CHECK(c == a);
    MiPoolFree(&M.mm.Pool, b, tag);
    MiPoolFree(&M.mm.Pool, c, tag);
    finish_checks();
    HostTeardown(&M);
}

static int      cs_stopped;
static uint8_t *cs_b;
static ULONG64  cs_live;

static void test_corruption_stops(void)
{
    setup(1);
    static ULONG tag;
    tag = TAG('C', 'o', 'r', 'r');
#define EXPECT_STOP(label, reason, stmt)                                                           \
    do {                                                                                           \
        HOST_CATCH(cs_stopped, stmt);                                                              \
        if (!cs_stopped)                                                                           \
            FAILF("%s: not detected", label);                                                      \
        if (g_stop.code != KAMOS_STOP_POOL_CORRUPTION || g_stop.p[0] != (reason))                  \
            FAILF("%s: stop code %d reason %lu, expected reason %d", label, g_stop.code,           \
                  (unsigned long)g_stop.p[0], (int)(reason));                                      \
        if (g_stop.lockDepthAtStop != 0)                                                           \
            FAILF("%s: stopped with a lock held", label);                                          \
    } while (0)

    /* Double free, small. The live count must not drop twice. */
    cs_b = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 40, tag);
    MiPoolFree(&M.mm.Pool, cs_b, tag);
    cs_live = M.mm.Pool.Live;
    EXPECT_STOP("double free", MM_POOL_STOP_DOUBLE_FREE, MiPoolFree(&M.mm.Pool, cs_b, tag));
    KT_CHECK_EQ(M.mm.Pool.Live, cs_live);

    /* Double free, large: keep the pages mapped so that the header can still be read. */
    g_poolQuarantine = 1;
    cs_b             = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 9000, tag);
    MiPoolFree(&M.mm.Pool, cs_b, tag);
    EXPECT_STOP("double free of a large block", MM_POOL_STOP_DOUBLE_FREE,
                MiPoolFree(&M.mm.Pool, cs_b, tag));
    g_poolQuarantine = 0;

    /* Wrong tag. */
    cs_b    = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 40, tag);
    cs_live = M.mm.Pool.Live;
    EXPECT_STOP("wrong tag", MM_POOL_STOP_WRONG_TAG,
                MiPoolFree(&M.mm.Pool, cs_b, TAG('O', 't', 'h', 'r')));
    KT_CHECK_EQ(M.mm.Pool.Live, cs_live);

    /* Overrun by one byte, small and large. */
    cs_b[40] ^= 0xFF;
    EXPECT_STOP("overrun by one byte", MM_POOL_STOP_OVERRUN, MiPoolFree(&M.mm.Pool, cs_b, tag));
    cs_b[40] ^= 0xFF;
    MiPoolFree(&M.mm.Pool, cs_b, tag); /* restored: frees normally */
    cs_b = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 9000, tag);
    cs_b[9000 + 7] ^= 0x01;
    EXPECT_STOP("large overrun", MM_POOL_STOP_OVERRUN, MiPoolFree(&M.mm.Pool, cs_b, tag));
    cs_b[9000 + 7] ^= 0x01;
    MiPoolFree(&M.mm.Pool, cs_b, tag);

    /* Underrun into the header. */
    cs_b = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 40, tag);
    uint8_t save[32];
    memcpy(save, cs_b - 32, 32);
    memset(cs_b - 32, 0, 8); /* the magic */
    EXPECT_STOP("header magic clobbered", MM_POOL_STOP_BAD_HEADER,
                MiPoolFree(&M.mm.Pool, cs_b, tag));
    memcpy(cs_b - 32, save, 32);
    memset(cs_b - 24, 0x41, 4); /* the tag */
    EXPECT_STOP("header tag clobbered", MM_POOL_STOP_WRONG_TAG, MiPoolFree(&M.mm.Pool, cs_b, tag));
    memcpy(cs_b - 32, save, 32);
    memset(cs_b - 16, 0, 8); /* the size */
    EXPECT_STOP("header size clobbered", MM_POOL_STOP_BAD_HEADER,
                MiPoolFree(&M.mm.Pool, cs_b, tag));
    memcpy(cs_b - 32, save, 32);

    /* Pointers that are not blocks. */
    EXPECT_STOP("unaligned pointer", MM_POOL_STOP_BAD_POINTER,
                MiPoolFree(&M.mm.Pool, cs_b + 1, tag));
    EXPECT_STOP("pointer into a block", MM_POOL_STOP_BAD_HEADER,
                MiPoolFree(&M.mm.Pool, cs_b + 16, tag));
    MiPoolFree(&M.mm.Pool, cs_b, tag);

    /* Write after free: found when the block is handed out again. */
    cs_b = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 40, tag);
    MiPoolFree(&M.mm.Pool, cs_b, tag);
    cs_b[30] = 0x42;
    EXPECT_STOP("write after free", MM_POOL_STOP_WRITE_AFTER_FREE,
                (void)MiPoolAllocate(&M.mm.Pool, NonPagedPool, 40, tag));

    HostTeardown(&M); /* the corrupt block stays out of the way: teardown releases every page */
    setup(1);
    /* Leaving the pool with live blocks is refused. */
    cs_b = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 40, tag);
    EXPECT_STOP("release with live blocks", MM_POOL_STOP_LIVE_AT_RELEASE,
                MiPoolReleaseAll(&M.mm.Pool));
    MiPoolFree(&M.mm.Pool, cs_b, tag);
#undef EXPECT_STOP
    finish_checks();
    HostTeardown(&M);
}

static void test_irql_rules(void)
{
    setup(1);
    ULONG tag = TAG('I', 'r', 'q', 'l');
    int   stopped;
    void *p    = NULL;
    g_hostIrql = PASSIVE_LEVEL;
    p          = MiPoolAllocate(&M.mm.Pool, PagedPool, 40, tag);
    KT_CHECK(p != NULL);
    MiPoolFree(&M.mm.Pool, p, tag);
    g_hostIrql = DISPATCH_LEVEL;
    p          = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 40, tag);
    KT_CHECK(p != NULL);
    MiPoolFree(&M.mm.Pool, p, tag);
    HOST_CATCH(stopped, (void)MiPoolAllocate(&M.mm.Pool, PagedPool, 40, tag));
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.code, KAMOS_STOP_IRQL_VIOLATION);
    g_hostIrql = DEVICE_LEVEL;
    HOST_CATCH(stopped, (void)MiPoolAllocate(&M.mm.Pool, NonPagedPool, 40, tag));
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.code, KAMOS_STOP_IRQL_VIOLATION);
    g_hostIrql = PASSIVE_LEVEL;
    finish_checks();
    HostTeardown(&M);
}

static void test_pool_lock_rank(void)
{
    setup(1);
    ULONG tag = TAG('L', 'o', 'c', 'k');
    memset(g_locks.acquired, 0, sizeof g_locks.acquired);
    g_locks.total = 0;
    void *a =
        MiPoolAllocate(&M.mm.Pool, NonPagedPool, 40, tag); /* first block: also fetches a page */
    KT_CHECK_EQ(g_locks.acquired[LOCK_RANK_POOL], 2); /* once to look, once to insert the page */
    KT_CHECK_EQ(g_locks.total, 2);
    void *b = MiPoolAllocate(&M.mm.Pool, NonPagedPool, 40, tag);
    KT_CHECK_EQ(g_locks.acquired[LOCK_RANK_POOL], 3);
    MiPoolFree(&M.mm.Pool, a, tag);
    MiPoolFree(&M.mm.Pool, b, tag);
    KT_CHECK_EQ(g_locks.acquired[LOCK_RANK_POOL], 5);
    void *c =
        MiPoolAllocate(&M.mm.Pool, NonPagedPool, 20000, tag); /* large: provider outside the lock */
    MiPoolFree(&M.mm.Pool, c, tag);
    KT_CHECK_EQ(g_locks.total, 7);
    finish_checks();
    HostTeardown(&M);
}

int main(void)
{
    static const kt_case cases[] = {
        KT_CASE("mm.pool", test_sizes_alignment_and_classes),
        KT_CASE("mm.pool", test_refusals),
        KT_CASE("mm.pool", test_random_sequences),
        KT_CASE("mm.pool", test_tag_table_overflow),
        KT_CASE("mm.pool", test_debug_patterns),
        KT_CASE("mm.pool", test_types_are_separate),
        KT_CASE("mm.pool", test_corruption_stops),
        KT_CASE("mm.pool", test_irql_rules),
        KT_CASE("mm.pool", test_pool_lock_rank),
    };
    return kt_run(cases, KAMOS_ARRAY_SIZE(cases)) ? 1 : 0;
}

/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Randomised map / unmap / protect / translate against a model. Owner: A04.
 * Oracles: a per-page model kept by the test (present, physical address, protection), the
 * independent walker (mm_dump.h, built from the SDM layout), the frame snapshot, and the
 * rollback guarantee (a failed call changes nothing).
 * Three windows are chosen to straddle the boundaries where tables are shared or split:
 *   0 user, across a 2 MiB boundary (page-table page shared by two directory entries)
 *   1 user, across the PML4 slot at 512 GiB (two PDPTs)
 *   2 kernel pool, across a 1 GiB boundary (two page directories under one PDPT)
 */
#define KTEST_HOST 1
#include <ktest.h>

#include <stdio.h>
#include <string.h>

#include "mm_dump.h"

#define R MM_PROT_READ
#define W MM_PROT_WRITE
#define X MM_PROT_EXECUTE
#define U MM_PROT_USER

#define NPAGES   64u
#define NWINDOWS 3u
#define NFRAMES  24u

static HostMachine M;
static Dump        g_dump;
static char        g_msg[200];

#define FAILF(...)                                                                                 \
    do {                                                                                           \
        snprintf(g_msg, sizeof g_msg, __VA_ARGS__);                                                \
        kt__fail(__FILE__, __LINE__, g_msg, 0, 0, 0);                                              \
    } while (0)

typedef struct ModelPage {
    int      present;
    uint64_t pa;
    ULONG    prot;
} ModelPage;

static const uint64_t g_base[NWINDOWS] = {
    UINT64_C(0x200000) - 32 * 4096,
    UINT64_C(0x8000000000) - 32 * 4096,
    MI_POOL_BASE + UINT64_C(0x40000000) - 32 * 4096,
};

static ModelPage g_model[NWINDOWS][NPAGES];
static uint64_t  g_frames[NFRAMES];

static ULONG random_prot(HostRng *r, unsigned w)
{
    static const ULONG k[] = {R, R | W, R | X};
    ULONG              p   = k[HostRandBelow(r, 3)];
    return w < 2 ? (p | U) : p;
}

static uint64_t random_pa(HostRng *r, unsigned w)
{
    if (w < 2)
        return g_frames[HostRandBelow(r, NFRAMES)];
    return UINT64_C(0x300000) + HostRandBelow(r, 4096) * 4096; /* any aligned address will do */
}

static ADDRESS_SPACE *space_of(ADDRESS_SPACE *user, unsigned w)
{
    return w < 2 ? user : &M.mm.KernelSpace;
}

static uint64_t leaf_of_model(const ModelPage *m)
{
    return x86o_pte(m->pa, (m->prot & W) != 0, (m->prot & X) != 0, (m->prot & U) != 0);
}

static int verify(ADDRESS_SPACE *user, unsigned op, uint64_t seed)
{
    for (unsigned w = 0; w < NWINDOWS; w++) {
        ADDRESS_SPACE *s = space_of(user, w);
        dump_space(&M.db, s->Root, w == 2, &g_dump);
        if (g_dump.defects != 0) {
            FAILF("seed %lu op %u: walker found a defect of kind %u", (unsigned long)seed, op,
                  g_dump.firstDefectKind);
            return 0;
        }
        unsigned inWindow = 0;
        for (unsigned i = 0; i < g_dump.n; i++)
            if (g_dump.leaf[i].va >= g_base[w] && g_dump.leaf[i].va < g_base[w] + NPAGES * 4096)
                inWindow++;
        unsigned expect = 0;
        for (unsigned p = 0; p < NPAGES; p++) {
            const ModelPage *m = &g_model[w][p];
            uint64_t         e = dump_find(&g_dump, g_base[w] + p * 4096);
            if (m->present) {
                expect++;
                if (e != leaf_of_model(m)) {
                    FAILF("seed %lu op %u: window %u page %u entry %#lx, model %#lx",
                          (unsigned long)seed, op, w, p, (unsigned long)e,
                          (unsigned long)leaf_of_model(m));
                    return 0;
                }
            } else if (e != 0) {
                FAILF("seed %lu op %u: window %u page %u mapped but not in the model",
                      (unsigned long)seed, op, w, p);
                return 0;
            }
        }
        if (inWindow != expect) {
            FAILF("seed %lu op %u: window %u has %u leaves, model %u", (unsigned long)seed, op, w,
                  inWindow, expect);
            return 0;
        }
    }
    return 1;
}

static void test_random_against_model(void)
{
    static const uint64_t seeds[] = {1,   2,   3,   5,   8,   13,   21,   34,   55,   89,
                                     144, 233, 377, 610, 987, 1597, 2584, 4181, 6765, 10946};
    for (unsigned si = 0; si < KAMOS_ARRAY_SIZE(seeds); si++) {
        uint64_t seed = seeds[si];
        HostMachineSmall(&M, 16);
        HostInitMm(&M, 1);
        HostSnap base;
        HostSnapTake(&M.db, &base);
        for (unsigned i = 0; i < NFRAMES; i++) {
            PHYSICAL_ADDRESS pa;
            KT_REQUIRE_EQ(MiAllocateFrame(&M.db, MmFrameActive, true, &pa), STATUS_SUCCESS);
            g_frames[i] = pa;
        }
        HostSnap withFrames;
        HostSnapTake(&M.db, &withFrames);
        ADDRESS_SPACE *user;
        KT_REQUIRE_EQ(MiCreateAddressSpace(&M.mm, &user), STATUS_SUCCESS);
        memset(g_model, 0, sizeof g_model);
        HostRng rng = {seed * UINT64_C(0x9E3779B97F4A7C15) + 1};

        unsigned injected = 0, rolledBack = 0, okMaps = 0, collisions = 0, okUnmaps = 0, okProt = 0,
                 badRange = 0;
        for (unsigned op = 0; op < 2500; op++) {
            unsigned       w     = (unsigned)HostRandBelow(&rng, NWINDOWS);
            ADDRESS_SPACE *s     = space_of(user, w);
            unsigned       first = (unsigned)HostRandBelow(&rng, NPAGES);
            unsigned       count = 1 + (unsigned)HostRandBelow(&rng, 9);
            if (first + count > NPAGES)
                count = NPAGES - first;
            uint64_t va        = g_base[w] + (uint64_t)first * 4096;
            int      anyMapped = 0, allMapped = 1;
            for (unsigned p = first; p < first + count; p++) {
                anyMapped |= g_model[w][p].present;
                allMapped &= g_model[w][p].present;
            }
            unsigned kind = (unsigned)HostRandBelow(&rng, 100);
            NTSTATUS st;
            if (kind < 40) {
                uint64_t pa0  = random_pa(&rng, w);
                ULONG    prot = random_prot(&rng, w);
                if (w < 2)
                    count = 1; /* user frames are single pages */
                anyMapped = 0;
                for (unsigned p = first; p < first + count; p++)
                    anyMapped |= g_model[w][p].present;
                int inject = HostRandBelow(&rng, 100) < 3;
                if (inject) {
                    g_tableFailAfter = (long)HostRandBelow(&rng, 4);
                    injected++;
                }
                HostSnap before;
                HostSnapTake(&M.db, &before);
                st               = MiMapPages(s, va, pa0, count, prot);
                g_tableFailAfter = -1;
                if (st == STATUS_SUCCESS) {
                    if (anyMapped)
                        FAILF("seed %lu op %u: map over a mapped page succeeded",
                              (unsigned long)seed, op);
                    for (unsigned p = 0; p < count; p++)
                        g_model[w][first + p] = (ModelPage){1, pa0 + (uint64_t)p * 4096, prot};
                    okMaps++;
                } else if (st == STATUS_NO_MEMORY && inject) {
                    rolledBack++;
                } else if (st == STATUS_OBJECT_NAME_COLLISION && anyMapped) {
                    collisions++;
                } else {
                    FAILF("seed %lu op %u: map returned %#x (inject %d, anyMapped %d)",
                          (unsigned long)seed, op, (unsigned)st, inject, anyMapped);
                }
                if (st != STATUS_SUCCESS && !HostSnapSame(&M.db, &before))
                    FAILF("seed %lu op %u: failed map %#x changed the frame database",
                          (unsigned long)seed, op, (unsigned)st);
                HostSnapFree(&before);
            } else if (kind < 65) {
                st = MiUnmapPages(s, va, count);
                if (allMapped) {
                    if (st != STATUS_SUCCESS)
                        FAILF("seed %lu op %u: unmap of mapped pages returned %#x",
                              (unsigned long)seed, op, (unsigned)st);
                    for (unsigned p = first; p < first + count; p++)
                        g_model[w][p].present = 0;
                    okUnmaps++;
                } else if (st != STATUS_INVALID_PARAMETER) {
                    FAILF("seed %lu op %u: unmap of a hole returned %#x", (unsigned long)seed, op,
                          (unsigned)st);
                } else {
                    badRange++;
                }
            } else if (kind < 85) {
                ULONG prot = random_prot(&rng, w);
                st         = MiProtectPages(s, va, count, prot);
                if (allMapped) {
                    if (st != STATUS_SUCCESS)
                        FAILF("seed %lu op %u: protect returned %#x", (unsigned long)seed, op,
                              (unsigned)st);
                    for (unsigned p = first; p < first + count; p++)
                        g_model[w][p].prot = prot;
                    okProt++;
                } else if (st != STATUS_INVALID_PARAMETER) {
                    FAILF("seed %lu op %u: protect of a hole returned %#x", (unsigned long)seed, op,
                          (unsigned)st);
                }
            } else {
                PHYSICAL_ADDRESS pa   = 0;
                ULONG            prot = 0;
                uint32_t         off  = (uint32_t)HostRandBelow(&rng, 4096);
                st                    = MiTranslate(s, va + off, &pa, &prot);
                const ModelPage *m    = &g_model[w][first];
                if (m->present) {
                    if (st != STATUS_SUCCESS || pa != m->pa + off || prot != m->prot)
                        FAILF("seed %lu op %u: translate %#x pa %#lx prot %#x, model %#lx %#x",
                              (unsigned long)seed, op, (unsigned)st, (unsigned long)pa, prot,
                              (unsigned long)(m->pa + off), m->prot);
                } else if (st != STATUS_OBJECT_NAME_NOT_FOUND) {
                    FAILF("seed %lu op %u: translate of a hole returned %#x", (unsigned long)seed,
                          op, (unsigned)st);
                }
            }
            if (kt__st.cur_failed)
                return;
            if (op % 100 == 99 && !verify(user, op, seed))
                return;
        }
        if (!verify(user, 9999, seed))
            return;
        KT_CHECK(okMaps > 100 && okUnmaps > 50 && okProt > 50 &&
                 collisions > 20); /* the run did exercise every path */
        KT_CHECK(injected > 10);
        (void)rolledBack;
        (void)badRange;

        /* Tear down: unmap what is left, then the tables must be exactly those of the start. */
        for (unsigned w = 0; w < NWINDOWS; w++)
            for (unsigned p = 0; p < NPAGES; p++)
                if (g_model[w][p].present)
                    KT_REQUIRE_EQ(
                        MiUnmapPages(space_of(user, w), g_base[w] + (uint64_t)p * 4096, 1),
                        STATUS_SUCCESS);
        dump_space(&M.db, user->Root, 0, &g_dump);
        KT_CHECK_EQ(g_dump.n, 0);
        KT_CHECK_EQ(g_dump.tables, 1); /* only the root is left: invariant I */
        MiDestroyAddressSpace(user);
        KT_CHECK(HostSnapSame(&M.db, &withFrames)); /* kernel tables back to where they were */
        for (unsigned i = 0; i < NFRAMES; i++)
            KT_CHECK_EQ(MiDereferenceFrame(&M.db, g_frames[i], MmFrameActive), 0);
        KT_CHECK(HostSnapSame(&M.db, &base));
        KT_CHECK_EQ(HostCheckFrames(&M), 0);
        KT_CHECK_EQ(g_locks.violations, 0);
        KT_CHECK_EQ(g_locks.depth, 0);
        KT_CHECK_EQ(g_simWild, 0);
        HostSnapFree(&base);
        HostSnapFree(&withFrames);
        HostTeardown(&M);
    }
}

int main(void)
{
    static const kt_case cases[] = {
        KT_CASE("mm.random", test_random_against_model),
    };
    return kt_run(cases, KAMOS_ARRAY_SIZE(cases)) ? 1 : 0;
}

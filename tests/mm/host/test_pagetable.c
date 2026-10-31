/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Page tables on the host. Owner: A04.
 * Every test reads the entries back from the simulated physical memory with a walker that shares
 * no code with the memory manager (mm_dump.h) and judges them with x86_oracle.h (A10). A failed
 * operation must leave the whole structure, the frame database and the invalidation log exactly
 * as they were.
 */
#define KTEST_HOST 1
#include <ktest.h>

#include <stdio.h>

#include "mm_dump.h"

#define R MM_PROT_READ
#define W MM_PROT_WRITE
#define X MM_PROT_EXECUTE
#define U MM_PROT_USER

static HostMachine    M;
static Dump           D0, D1;
static HostSnap       S0;
static ADDRESS_SPACE *S;
static char           g_msg[200];

#define FAILF(...)                                                                                 \
    do {                                                                                           \
        snprintf(g_msg, sizeof g_msg, __VA_ARGS__);                                                \
        KT_FAIL(g_msg);                                                                            \
    } while (0)

static void setup(unsigned mib)
{
    HostMachineSmall(&M, mib);
    HostInitMm(&M, 1);
    if (!NT_SUCCESS(MiCreateAddressSpace(&M.mm, &S)))
        abort();
}

static void teardown(void)
{
    HostTeardown(&M);
}

static PHYSICAL_ADDRESS uframe(void)
{
    PHYSICAL_ADDRESS pa;
    if (!NT_SUCCESS(MiAllocateFrame(&M.db, MmFrameActive, true, &pa)))
        abort();
    return pa;
}

static void world_take(void)
{
    dump_space(&M.db, S->Root, 1, &D0);
    HostSnapTake(&M.db, &S0);
}

static int world_same(void)
{
    dump_space(&M.db, S->Root, 1, &D1);
    int ok = dump_same(&D0, &D1) && HostSnapSame(&M.db, &S0) && D1.defects == 0;
    HostSnapFree(&S0);
    return ok;
}

static void finish_checks(void)
{
    KT_CHECK_EQ(g_locks.violations, 0);
    KT_CHECK_EQ(g_locks.depth, 0);
    KT_CHECK_EQ(g_simWild, 0);
}

/* A call that must fail and change nothing. */
#define CASE(label, want, call)                                                                    \
    do {                                                                                           \
        size_t inv_ = g_invalCount;                                                                \
        world_take();                                                                              \
        NTSTATUS st_   = (call);                                                                   \
        int      same_ = world_same();                                                             \
        if (st_ != (want))                                                                         \
            FAILF("%s: status %#x, expected %#x", label, (unsigned)st_, (unsigned)(want));         \
        if (!same_)                                                                                \
            FAILF("%s: state changed (defects %u, kind %u)", label, D1.defects,                    \
                  D1.firstDefectKind);                                                             \
        if (g_invalCount != inv_)                                                                  \
            FAILF("%s: invlpg issued", label);                                                     \
        if (g_locks.violations != 0 || g_locks.depth != 0)                                         \
            FAILF("%s: lock problem", label);                                                      \
    } while (0)

static void test_pte_layout(void)
{
    setup(12);
    static const struct {
        ULONG prot;
        int   w, x, u;
    } v[] = {
        {R, 0, 0, 0},     {R | W, 1, 0, 0},     {R | X, 0, 1, 0},
        {R | U, 0, 0, 1}, {R | W | U, 1, 0, 1}, {R | X | U, 0, 1, 1},
    };
    ULONG64          tablesBefore = M.db.StateCount[MmFramePageTable];
    PHYSICAL_ADDRESS owned[6];
    for (unsigned i = 0; i < 6; i++) {
        uint64_t         va;
        PHYSICAL_ADDRESS pa;
        if (v[i].u) {
            va = 0x400000 + 0x1000u * i;
            pa = owned[i] = uframe();
        } else {
            va = MI_POOL_BASE + 0x1000u * i;
            pa = (UINT64_C(1) << 39) | (0x5000u + 0x1000u * i); /* high bit of a 40-bit space */
            owned[i] = 0;
        }
        KT_REQUIRE_EQ(MiMapPages(S, va, pa, 1, v[i].prot), STATUS_SUCCESS);
        uint64_t e = oracle_leaf(S->Root, va);
        if (e != x86o_pte(pa, v[i].w, v[i].x, v[i].u))
            FAILF("prot %#x: entry %#lx, SDM layout %#lx", v[i].prot, (unsigned long)e,
                  (unsigned long)x86o_pte(pa, v[i].w, v[i].x, v[i].u));
    }
    /* The kernel half is shared: the kernel space sees the same kernel entries. */
    KT_CHECK_EQ(oracle_leaf(M.mm.KernelSpace.Root, MI_POOL_BASE + 0x1000),
                oracle_leaf(S->Root, MI_POOL_BASE + 0x1000));
    KT_CHECK_EQ(oracle_leaf(M.mm.KernelSpace.Root, 0x400000), 0); /* user half is not */

    dump_space(&M.db, S->Root, 1, &D0);
    KT_CHECK_EQ(D0.defects, 0);
    KT_CHECK_EQ(D0.n, 6);
    /* root + user PDPT, PD, PT + kernel PD, PT + 256 permanent PDPTs */
    KT_CHECK_EQ(D0.tables, 1 + 3 + 2 + 256);
    KT_CHECK_EQ(D0.kernelPdpts, 256);
    KT_CHECK_EQ(M.db.StateCount[MmFramePageTable], tablesBefore + 3 + 2);

    for (unsigned i = 0; i < 6; i++) {
        uint64_t va = v[i].u ? 0x400000 + 0x1000u * i : MI_POOL_BASE + 0x1000u * i;
        KT_REQUIRE_EQ(MiUnmapPages(S, va, 1), STATUS_SUCCESS);
        KT_CHECK_EQ(oracle_leaf(S->Root, va), 0);
        if (owned[i])
            (void)MiDereferenceFrame(&M.db, owned[i], MmFrameActive);
    }
    MiDestroyAddressSpace(S);
    KT_CHECK_EQ(M.db.StateCount[MmFramePageTable], tablesBefore - 1); /* the root of S went too */
    KT_CHECK_EQ(MiFrameDbVerify(&M.db), 0);
    finish_checks();
    teardown();
}

static void test_rules_rejected(void)
{
    setup(12);
    PHYSICAL_ADDRESS fa     = uframe();
    PHYSICAL_ADDRESS pair_a = uframe();
    PHYSICAL_ADDRESS pair_b = uframe();
    PHYSICAL_ADDRESS freed  = pair_b;
    KT_REQUIRE_EQ(pair_b, pair_a + 0x1000); /* the allocator hands out neighbours first */
    KT_REQUIRE_EQ(MiDereferenceFrame(&M.db, pair_b, MmFrameActive), 0);
    PHYSICAL_ADDRESS beyondDb = M.db.FrameCount << PAGE_SHIFT;
    const NTSTATUS   BAD      = STATUS_INVALID_PARAMETER;
    const NTSTATUS   COL      = STATUS_OBJECT_NAME_COLLISION;
    PHYSICAL_ADDRESS kpa      = 0x200000;

    /* Alignment, canonical form, guards, regions (ADR-0005). */
    CASE("va misaligned", BAD, MiMapPages(S, 0x400001, fa, 1, R | U));
    CASE("pa misaligned", BAD, MiMapPages(S, 0x400000, fa + 1, 1, R | U));
    CASE("non-canonical (low)", BAD, MiMapPages(S, UINT64_C(0x0000800000000000), fa, 1, R | U));
    CASE("non-canonical (high)", BAD, MiMapPages(S, UINT64_C(0xFFFF7FFFFFFFF000), fa, 1, R));
    CASE("null page", BAD, MiMapPages(S, 0, fa, 1, R | U));
    CASE("below the user range", BAD, MiMapPages(S, 0xF000, fa, 1, R | U));
    CASE("range crosses the null guard", BAD, MiMapPages(S, 0xE000, fa, 4, R | U));
    CASE("top guard", BAD, MiMapPages(S, UINT64_C(0x00007FFFFFFF0000), fa, 1, R | U));
    CASE("last page of the address half", BAD,
         MiMapPages(S, UINT64_C(0x00007FFFFFFFF000), fa, 1, R | U));
    CASE("range crosses the top guard", BAD,
         MiMapPages(S, UINT64_C(0x00007FFFFFFEF000), fa, 2, R | U));
    CASE("reserved kernel range", BAD, MiMapPages(S, UINT64_C(0xFFFF888000000000), kpa, 1, R));
    CASE("between reserved and pool", BAD, MiMapPages(S, UINT64_C(0xFFFF9FFFFFFFF000), kpa, 1, R));
    CASE("above the pool region", BAD, MiMapPages(S, UINT64_C(0xFFFFA10000000000), kpa, 1, R));
    CASE("range leaves the direct map", BAD,
         MiMapPages(S, UINT64_C(0xFFFF87FFFFFFF000), kpa, 2, R));
    CASE("range wraps the address space", BAD,
         MiMapPages(S, UINT64_C(0xFFFFFFFFFFFFF000), kpa, 2, R));
    CASE("count 0", BAD, MiMapPages(S, 0x400000, fa, 0, R | U));
    CASE("count 2^36 + 1", BAD, MiMapPages(S, 0x400000, fa, (UINT64_C(1) << 36) + 1, R | U));

    /* Protection. */
    CASE("W and X", BAD, MiMapPages(S, 0x400000, fa, 1, R | W | X | U));
    CASE("W and X (kernel)", BAD, MiMapPages(S, MI_POOL_BASE, kpa, 1, R | W | X));
    CASE("no protection", BAD, MiMapPages(S, 0x400000, fa, 1, 0));
    CASE("write without read", BAD, MiMapPages(S, 0x400000, fa, 1, W | U));
    CASE("execute without read", BAD, MiMapPages(S, 0x400000, fa, 1, X | U));
    CASE("unknown bit", BAD, MiMapPages(S, 0x400000, fa, 1, R | U | 0x10));
    CASE("user flag on a kernel address", BAD, MiMapPages(S, MI_POOL_BASE, kpa, 1, R | U));
    CASE("kernel mapping at a user address", BAD, MiMapPages(S, 0x400000, fa, 1, R));

    /* Physical address. */
    CASE("pa beyond 40 bits", BAD, MiMapPages(S, MI_POOL_BASE, UINT64_C(1) << 40, 1, R));
    CASE("range crosses the physical limit", BAD,
         MiMapPages(S, MI_POOL_BASE, (UINT64_C(1) << 40) - 0x1000, 2, R));

    /* User pages may only be frames the caller owns (state Active): never kernel memory. */
    CASE("user map of a free frame", BAD, MiMapPages(S, 0x400000, freed, 1, R | U));
    CASE("user map of a page table", BAD, MiMapPages(S, 0x400000, S->Root, 1, R | W | U));
    CASE("user map of a reserved frame", BAD, MiMapPages(S, 0x400000, 0x1000, 1, R | U));
    CASE("user map beyond the database", BAD, MiMapPages(S, 0x400000, beyondDb, 1, R | U));
    CASE("user map, second frame is free", BAD, MiMapPages(S, 0x400000, pair_a, 2, R | W | U));

    /* Unmap and protect of what is not there. */
    CASE("unmap misaligned", BAD, MiUnmapPages(S, 0x400001, 1));
    CASE("unmap non-canonical", BAD, MiUnmapPages(S, UINT64_C(0x0000800000000000), 1));
    CASE("unmap null page", BAD, MiUnmapPages(S, 0, 1));
    CASE("unmap, nothing mapped", BAD, MiUnmapPages(S, 0x400000, 1));
    CASE("unmap kernel, nothing mapped", BAD, MiUnmapPages(S, MI_POOL_BASE, 1));
    CASE("unmap count 0", BAD, MiUnmapPages(S, 0x400000, 0));
    CASE("protect, nothing mapped", BAD, MiProtectPages(S, 0x400000, 1, R | U));
    CASE("protect W and X", BAD, MiProtectPages(S, 0x400000, 1, R | W | X | U));

    /* A real mapping, then collisions and half-mapped ranges. */
    KT_REQUIRE_EQ(MiMapPages(S, 0x401000, fa, 1, R | W | U), STATUS_SUCCESS);
    KT_REQUIRE_EQ(MiMapPages(S, MI_POOL_BASE + 0x1000, kpa, 1, R | W), STATUS_SUCCESS);
    CASE("map the same page again", COL, MiMapPages(S, 0x401000, fa, 1, R | U));
    CASE("map over the second page of a range", COL, MiMapPages(S, 0x400000, fa, 2, R | U));
    CASE("map over a kernel page", COL, MiMapPages(S, MI_POOL_BASE, kpa, 3, R));
    CASE("unmap, range half mapped", BAD, MiUnmapPages(S, 0x400000, 3));
    CASE("unmap kernel, range half mapped", BAD, MiUnmapPages(S, MI_POOL_BASE, 2));
    CASE("protect, range half mapped", BAD, MiProtectPages(S, 0x401000, 2, R | U));
    CASE("protect kernel page as user", BAD, MiProtectPages(S, MI_POOL_BASE + 0x1000, 1, R | U));
    CASE("protect user page as kernel", BAD, MiProtectPages(S, 0x401000, 1, R));
    CASE("protect user page W and X", BAD, MiProtectPages(S, 0x401000, 1, R | W | X | U));

    /* The accepted counterparts, so that the rejections are not a refusal of everything. */
    KT_CHECK_EQ(MiProtectPages(S, 0x401000, 1, R | X | U), STATUS_SUCCESS);
    KT_CHECK_EQ(oracle_leaf(S->Root, 0x401000), x86o_pte(fa, 0, 1, 1));
    KT_CHECK_EQ(MiUnmapPages(S, 0x401000, 1), STATUS_SUCCESS);
    KT_CHECK_EQ(MiUnmapPages(S, MI_POOL_BASE + 0x1000, 1), STATUS_SUCCESS);
    KT_CHECK_EQ(MiMapPages(S, 0x10000, fa, 1, R | U), STATUS_SUCCESS); /* first user page */
    KT_CHECK_EQ(MiMapPages(S, UINT64_C(0x00007FFFFFFEF000), fa, 1, R | U),
                STATUS_SUCCESS); /* last */
    KT_CHECK_EQ(MiMapPages(S, UINT64_C(0xFFFFFFFFFFFFF000), kpa, 1, R),
                STATUS_SUCCESS); /* top page */
    KT_CHECK_EQ(MiMapPages(S, UINT64_C(0xFFFF87FFFFFFF000), kpa, 1, R),
                STATUS_SUCCESS); /* end of direct map */
    KT_CHECK_EQ(MiUnmapPages(S, 0x10000, 1), STATUS_SUCCESS);
    KT_CHECK_EQ(MiUnmapPages(S, UINT64_C(0x00007FFFFFFEF000), 1), STATUS_SUCCESS);
    KT_CHECK_EQ(MiUnmapPages(S, UINT64_C(0xFFFFFFFFFFFFF000), 1), STATUS_SUCCESS);
    KT_CHECK_EQ(MiUnmapPages(S, UINT64_C(0xFFFF87FFFFFFF000), 1), STATUS_SUCCESS);
    dump_space(&M.db, S->Root, 1, &D0);
    KT_CHECK_EQ(D0.n, 0);
    KT_CHECK_EQ(D0.defects, 0);
    KT_CHECK_EQ(D0.tables, 1 + 256); /* everything pruned again */
    finish_checks();
    teardown();
}

static void test_map_translate_unmap_and_tlb(void)
{
    setup(12);
    PHYSICAL_ADDRESS fr[10];
    for (unsigned i = 0; i < 10; i++)
        fr[i] = uframe();
    ULONG64 tablesBefore = M.db.StateCount[MmFramePageTable];
    dump_space(&M.db, S->Root, 1, &D0);
    const uint64_t base = 0x70000000;

    /* S is not the current space: no processor caches its user translations. */
    HostInvalClear();
    for (unsigned i = 0; i < 10; i++)
        KT_REQUIRE_EQ(MiMapPages(S, base + 0x1000u * i, fr[i], 1, R | W | U), STATUS_SUCCESS);
    for (unsigned i = 0; i < 10; i++) {
        PHYSICAL_ADDRESS pa   = 0;
        ULONG            prot = 0;
        KT_REQUIRE_EQ(MiTranslate(S, base + 0x1000u * i + 0x123, &pa, &prot), STATUS_SUCCESS);
        KT_CHECK_EQ(pa, fr[i] + 0x123);
        KT_CHECK_EQ(prot, R | W | U);
        KT_CHECK_EQ(oracle_leaf(S->Root, base + 0x1000u * i), x86o_pte(fr[i], 1, 0, 1));
    }
    KT_CHECK_EQ(MiTranslate(S, base + 0x1000u * 10, NULL, NULL), STATUS_OBJECT_NAME_NOT_FOUND);
    KT_CHECK_EQ(MiTranslate(S, 0, NULL, NULL), STATUS_OBJECT_NAME_NOT_FOUND);
    KT_CHECK_EQ(MiTranslate(S, UINT64_C(0x0000900000000000), NULL, NULL), STATUS_INVALID_PARAMETER);
    KT_CHECK_EQ(MiUnmapPages(S, base, 10), STATUS_SUCCESS);
    KT_CHECK_EQ(g_invalCount, 0);
    dump_space(&M.db, S->Root, 1, &D1);
    KT_CHECK(dump_same(&D0, &D1)); /* tables pruned: back to the starting structure */
    KT_CHECK_EQ(M.db.StateCount[MmFramePageTable], tablesBefore);

    /* S is current: every changed page is invalidated, in order, and only those. */
    MiSwitchAddressSpace(S);
    KT_CHECK_EQ(g_hostRoot, S->Root);
    KT_CHECK(M.mm.Current == S);
    for (unsigned i = 0; i < 5; i++)
        KT_REQUIRE_EQ(MiMapPages(S, base + 0x1000u * i, fr[i], 1, R | U), STATUS_SUCCESS);
    KT_CHECK_EQ(g_invalCount, 0); /* a new translation needs no flush */
    KT_CHECK_EQ(MiProtectPages(S, base + 0x1000, 3, R | W | U), STATUS_SUCCESS);
    KT_REQUIRE_EQ(g_invalCount, 3);
    for (unsigned i = 0; i < 3; i++)
        KT_CHECK_EQ(g_inval[i], base + 0x1000u * (i + 1));
    HostInvalClear();
    KT_CHECK_EQ(MiUnmapPages(S, base, 5), STATUS_SUCCESS);
    KT_REQUIRE_EQ(g_invalCount, 5);
    for (unsigned i = 0; i < 5; i++)
        KT_CHECK_EQ(g_inval[i], base + 0x1000u * i);

    /* Kernel-half pages are visible in every space, so they are invalidated even when the
     * space used to change them is not current. */
    MiSwitchAddressSpace(&M.mm.KernelSpace);
    HostInvalClear();
    KT_REQUIRE_EQ(MiMapPages(S, MI_STACK_BASE, 0x300000, 3, R | W), STATUS_SUCCESS);
    KT_CHECK_EQ(MiProtectPages(S, MI_STACK_BASE, 3, R), STATUS_SUCCESS);
    KT_CHECK_EQ(g_invalCount, 3);
    KT_CHECK_EQ(MiUnmapPages(S, MI_STACK_BASE, 3), STATUS_SUCCESS);
    KT_CHECK_EQ(g_invalCount, 6);
    HostInvalClear();
    /* And a user page of S while the kernel space is current: nothing to flush. */
    KT_REQUIRE_EQ(MiMapPages(S, base, fr[0], 1, R | U), STATUS_SUCCESS);
    KT_CHECK_EQ(MiProtectPages(S, base, 1, R | W | U), STATUS_SUCCESS);
    KT_CHECK_EQ(MiUnmapPages(S, base, 1), STATUS_SUCCESS);
    KT_CHECK_EQ(g_invalCount, 0);

    dump_space(&M.db, S->Root, 1, &D1);
    KT_CHECK(dump_same(&D0, &D1));
    for (unsigned i = 0; i < 10; i++)
        (void)MiDereferenceFrame(&M.db, fr[i], MmFrameActive);
    finish_checks();
    teardown();
}

static void test_protect_keeps_hardware_bits(void)
{
    setup(12);
    PHYSICAL_ADDRESS fa = uframe();
    KT_REQUIRE_EQ(MiMapPages(S, 0x500000, fa, 1, R | W | U), STATUS_SUCCESS);
    uint64_t *t = dump_tbl(S->Root);
    for (int level = 4; level > 1; level--)
        t = dump_tbl(t[x86o_index(0x500000, level)] & X86O_ADDR_MASK);
    uint64_t *leaf = &t[x86o_index(0x500000, 1)];
    KT_CHECK_EQ(*leaf, x86o_pte(fa, 1, 0, 1));
    *leaf |= (UINT64_C(1) << 5) | (UINT64_C(1) << 6); /* accessed, dirty: set by the processor */
    KT_CHECK_EQ(MiProtectPages(S, 0x500000, 1, R | X | U), STATUS_SUCCESS);
    KT_CHECK_EQ(*leaf, x86o_pte(fa, 0, 1, 1) | (UINT64_C(1) << 5) | (UINT64_C(1) << 6));
    KT_CHECK_EQ(MiProtectPages(S, 0x500000, 1, R | U), STATUS_SUCCESS);
    KT_CHECK_EQ(*leaf, x86o_pte(fa, 0, 0, 1) | (UINT64_C(1) << 5) | (UINT64_C(1) << 6));
    PHYSICAL_ADDRESS pa;
    ULONG            prot;
    KT_REQUIRE_EQ(MiTranslate(S, 0x500000, &pa, &prot), STATUS_SUCCESS);
    KT_CHECK_EQ(prot, R | U);
    *leaf &= ~((UINT64_C(1) << 5) | (UINT64_C(1) << 6));
    KT_CHECK_EQ(MiUnmapPages(S, 0x500000, 1), STATUS_SUCCESS);
    (void)MiDereferenceFrame(&M.db, fa, MmFrameActive);
    finish_checks();
    teardown();
}

static uint64_t *entry_at(uint64_t root, uint64_t va, int level)
{
    uint64_t t = root;
    for (int l = 4; l > level; l--)
        t = dump_tbl(t)[x86o_index(va, l)] & X86O_ADDR_MASK;
    return &dump_tbl(t)[x86o_index(va, level)];
}

static void test_effective_rights_and_large_pages(void)
{
    setup(12);
    PHYSICAL_ADDRESS fa = uframe();
    PHYSICAL_ADDRESS pa;
    ULONG            prot;
    const uint64_t   va = 0x40200000; /* aligned to 2 MiB; first page of its PT */

    /* Effective rights: R/W and U/S are the AND over the levels, XD the OR (SDM, paging).
     * Page A is read/write/user (its leaf has XD); page B is read/execute/user (no W). */
    KT_REQUIRE_EQ(MiMapPages(S, va, fa, 1, R | W | X | U), STATUS_INVALID_PARAMETER); /* W and X */
    KT_REQUIRE_EQ(MiMapPages(S, va, fa, 1, R | W | U), STATUS_SUCCESS);
    KT_REQUIRE_EQ(MiMapPages(S, va + 0x1000, fa, 1, R | X | U), STATUS_SUCCESS);
    uint64_t *pd    = entry_at(S->Root, va, 2);
    uint64_t  saved = *pd;
    *pd &= ~X86O_RW;
    KT_REQUIRE_EQ(MiTranslate(S, va, &pa, &prot), STATUS_SUCCESS);
    KT_CHECK_EQ(prot, R | U); /* the PDE takes write away from A */
    KT_REQUIRE_EQ(MiTranslate(S, va + 0x1000, &pa, &prot), STATUS_SUCCESS);
    KT_CHECK_EQ(prot, R | X | U);
    *pd            = saved;
    uint64_t *pdpt = entry_at(S->Root, va, 3);
    saved          = *pdpt;
    *pdpt &= ~X86O_US;
    KT_REQUIRE_EQ(MiTranslate(S, va, &pa, &prot), STATUS_SUCCESS);
    KT_CHECK_EQ(prot, R | W); /* no longer user-accessible */
    KT_REQUIRE_EQ(MiTranslate(S, va + 0x1000, &pa, &prot), STATUS_SUCCESS);
    KT_CHECK_EQ(prot, R | X);
    *pdpt          = saved;
    uint64_t *pml4 = entry_at(S->Root, va, 4);
    saved          = *pml4;
    *pml4 |= X86O_XD;
    KT_REQUIRE_EQ(MiTranslate(S, va, &pa, &prot), STATUS_SUCCESS);
    KT_CHECK_EQ(prot, R | W | U);
    KT_REQUIRE_EQ(MiTranslate(S, va + 0x1000, &pa, &prot), STATUS_SUCCESS);
    KT_CHECK_EQ(prot, R | U); /* XD at the top removes execute from B */
    *pml4 = saved;
    KT_REQUIRE_EQ(MiTranslate(S, va, &pa, &prot), STATUS_SUCCESS);
    KT_CHECK_EQ(prot, R | W | U);
    KT_REQUIRE_EQ(MiTranslate(S, va + 0x1000, &pa, &prot), STATUS_SUCCESS);
    KT_CHECK_EQ(prot, R | X | U);

    /* A 2 MiB page (as a boot loader may leave): translated, never split, never overwritten. */
    uint64_t pdeSaved = *pd;
    *pd               = 0x800000 | X86O_P | X86O_RW | X86O_US | (UINT64_C(1) << 7);
    KT_REQUIRE_EQ(MiTranslate(S, va + 0x1234, &pa, &prot), STATUS_SUCCESS);
    KT_CHECK_EQ(pa, 0x801234);
    KT_CHECK_EQ(prot, R | W | U | X);
    KT_CHECK_EQ(MiMapPages(S, va + 0x1000, fa, 1, R | U), STATUS_OBJECT_NAME_COLLISION);
    KT_CHECK_EQ(MiUnmapPages(S, va, 1), STATUS_NOT_IMPLEMENTED);
    KT_CHECK_EQ(MiProtectPages(S, va, 1, R | U), STATUS_NOT_IMPLEMENTED);
    KT_CHECK_EQ(*pd, 0x800000 | X86O_P | X86O_RW | X86O_US | (UINT64_C(1) << 7));
    *pd = pdeSaved;

    /* A 1 GiB page. */
    uint64_t pdptSaved = *pdpt;
    *pdpt = UINT64_C(0x40000000) | X86O_P | X86O_RW | X86O_US | X86O_XD | (UINT64_C(1) << 7);
    KT_REQUIRE_EQ(MiTranslate(S, va + 0x1234, &pa, &prot), STATUS_SUCCESS);
    KT_CHECK_EQ(pa, UINT64_C(0x40000000) + 0x201234);
    KT_CHECK_EQ(prot, R | W | U);
    KT_CHECK_EQ(MiMapPages(S, va + 0x5000, fa, 1, R | U), STATUS_OBJECT_NAME_COLLISION);
    KT_CHECK_EQ(MiUnmapPages(S, va, 1), STATUS_NOT_IMPLEMENTED);
    *pdpt = pdptSaved;

    /* Corrupt entries stop the kernel instead of being followed. */
    int      stopped;
    uint64_t pmlSaved = *pml4;
    *pml4 |= (UINT64_C(1) << 7); /* PS in a PML4E is reserved */
    HOST_CATCH(stopped, (void)MiTranslate(S, va, &pa, &prot));
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.p[0], MM_STOP_BASE | MM_STOP_TABLE_CORRUPT);
    HOST_CATCH(stopped, (void)MiMapPages(S, va + 0x2000, fa, 1, R | U));
    KT_CHECK_EQ(stopped, 1);
    *pml4     = pmlSaved;
    pdptSaved = *pdpt;
    *pdpt |= UINT64_C(1) << 45; /* beyond the 40 physical bits of this machine */
    HOST_CATCH(stopped, (void)MiTranslate(S, va, &pa, &prot));
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.p[0], MM_STOP_BASE | MM_STOP_TABLE_CORRUPT);
    *pdpt = pdptSaved;

    KT_CHECK_EQ(MiUnmapPages(S, va, 2), STATUS_SUCCESS);
    dump_space(&M.db, S->Root, 1, &D0);
    KT_CHECK_EQ(D0.defects, 0);
    KT_CHECK_EQ(D0.tables, 1 + 256);
    (void)MiDereferenceFrame(&M.db, fa, MmFrameActive);
    finish_checks();
    teardown();
}

typedef struct Scenario {
    const char *name;
    uint64_t    va;
    size_t      count;
    ULONG       prot;
    int         neighbours; /* map one page before and one far away first */
    unsigned    expectNew;  /* tables the call must create */
} Scenario;

static void test_fault_injection_map(void)
{
    static const Scenario sc[] = {
        {"one user page, nothing around", 0x400000, 1, R | W | U, 0, 3},
        {"600 user pages over a PT edge", 0x7FF000, 600, R | U, 0, 5},
        {"user pages over a PML4 slot edge", UINT64_C(0x7FFFFFF000), 3, R | W | U, 0, 6},
        {"pool pages over a 1 GiB edge", MI_POOL_BASE + 0x3FFFF000, 4, R | W, 0, 4},
        {"image pages over a 2 MiB edge", MI_IMAGE_BASE + 0x1FF000, 3, R | X, 0, 3},
        {"520 image pages, two PTs", MI_IMAGE_BASE + 0x100000, 520, R | W, 0, 3},
        {"next to existing pages", 0x401000, 700, R | W | U, 1, 1},
    };
    for (unsigned si = 0; si < KAMOS_ARRAY_SIZE(sc); si++) {
        const Scenario *s = &sc[si];
        setup(12);
        int              user  = (s->prot & U) != 0;
        PHYSICAL_ADDRESS first = 0x300000; /* kernel ranges: any physical address will do */
        if (user) {
            /* A user range needs Active frames; the allocator hands out neighbours first. */
            first = uframe();
            for (size_t i = 1; i < s->count; i++)
                if (uframe() != first + 0x1000 * i)
                    FAILF("%s: frames are not contiguous", s->name);
        }
        if (s->neighbours) {
            PHYSICAL_ADDRESS n0 = uframe();
            KT_REQUIRE_EQ(MiMapPages(S, 0x400000, n0, 1, R | U), STATUS_SUCCESS);
            KT_REQUIRE_EQ(MiMapPages(S, UINT64_C(0x6000000000), n0, 1, R | U), STATUS_SUCCESS);
        }

        /* Success path: the number of tables created; map then unmap restores the structure. */
        world_take();
        g_tableAllocs    = 0;
        NTSTATUS st      = MiMapPages(S, s->va, first, s->count, s->prot);
        unsigned created = g_tableAllocs;
        if (st != STATUS_SUCCESS)
            FAILF("%s: map failed with %#x", s->name, (unsigned)st);
        if (created != s->expectNew)
            FAILF("%s: created %u tables, expected %u", s->name, created, s->expectNew);
        dump_space(&M.db, S->Root, 1, &D1);
        if (D1.defects != 0 || D1.n != D0.n + s->count || D1.tables != D0.tables + created)
            FAILF("%s: after map: %u defects, %u leaves, %u tables", s->name, D1.defects, D1.n,
                  D1.tables);
        KT_REQUIRE_EQ(MiUnmapPages(S, s->va, s->count), STATUS_SUCCESS);
        if (!world_same())
            FAILF("%s: map then unmap did not restore the structure", s->name);

        /* Fail the k-th table allocation, for every k that the call reaches. */
        for (unsigned k = 0; k < created; k++) {
            size_t inv = g_invalCount;
            world_take();
            g_tableFailAfter = (long)k;
            NTSTATUS r       = MiMapPages(S, s->va, first, s->count, s->prot);
            g_tableFailAfter = -1;
            int same         = world_same();
            if (r != STATUS_NO_MEMORY)
                FAILF("%s: failure %u gave status %#x", s->name, k, (unsigned)r);
            if (!same)
                FAILF("%s: state changed after the failure of allocation %u (defects %u kind %u)",
                      s->name, k, D1.defects, D1.firstDefectKind);
            if (g_invalCount != inv)
                FAILF("%s: invlpg after a failed map", s->name);
        }
        /* After all the failures the call still works. */
        KT_REQUIRE_EQ(MiMapPages(S, s->va, first, s->count, s->prot), STATUS_SUCCESS);
        if (g_locks.violations != 0 || g_locks.depth != 0)
            FAILF("%s: lock problem", s->name);
        teardown();
    }
    finish_checks();
}

static void test_address_space_creation_failures(void)
{
    HostMachineSmall(&M, 12);
    HostInitMm(&M, 1); /* no address space yet: the pool is empty */
    HostSnap snap;
    HostSnapTake(&M.db, &snap);
    ADDRESS_SPACE *s2 = (ADDRESS_SPACE *)(uintptr_t)0x1;
    g_poolFailAfter   = 0; /* the pool cannot get its first page */
    KT_CHECK_EQ(MiCreateAddressSpace(&M.mm, &s2), STATUS_NO_MEMORY);
    g_poolFailAfter = -1;
    KT_CHECK(s2 == NULL);
    KT_CHECK(HostSnapSame(&M.db, &snap));
    KT_CHECK_EQ(M.mm.Pool.Live, 0);

    g_tableFailAfter = 0; /* the root frame cannot be allocated */
    KT_CHECK_EQ(MiCreateAddressSpace(&M.mm, &s2), STATUS_NO_MEMORY);
    g_tableFailAfter = -1;
    KT_CHECK(s2 == NULL);
    KT_CHECK(HostSnapSame(&M.db, &snap));
    KT_CHECK_EQ(M.mm.Pool.Live, 0); /* the block taken for the object was given back */
    KT_CHECK_EQ(MiCreateAddressSpace(&M.mm, NULL), STATUS_INVALID_PARAMETER);
    HostSnapFree(&snap);
    finish_checks();
    teardown();
}

static void test_direct_map(void)
{
    HostMachineStandard(&M);
    HostInitMm(&M, 1);
    S = &M.mm.KernelSpace;
    KT_REQUIRE_EQ(MiBuildDirectMap(S, &M.boot), STATUS_SUCCESS);
    dump_space(&M.db, S->Root, 1, &D0);
    KT_CHECK_EQ(D0.defects, 0);
    /* Expected: every page that lies wholly in a usable or ACPI-reclaimable region. */
    unsigned expected = 0;
    for (ULONG64 pfn = 0; pfn < M.db.FrameCount + 16; pfn++) {
        uint64_t ps = pfn * 4096, pe = ps + 4096;
        int      in = 0;
        for (ULONG i = 0; i < M.boot.RegionCount; i++) {
            const BOOT_MEMORY_REGION *r = &M.boot.Regions[i];
            if ((r->Type == BootMemoryUsable || r->Type == BootMemoryAcpiReclaimable) &&
                r->Base <= ps && pe <= r->Base + r->Length)
                in = 1;
        }
        uint64_t e = oracle_leaf(S->Root, MI_DIRECT_MAP_BASE + ps);
        expected += in;
        if (in && e != x86o_pte(ps, 1, 0, 0))
            FAILF("page %#lx: entry %#lx, expected a read/write no-execute kernel entry",
                  (unsigned long)ps, (unsigned long)e);
        if (!in && e != 0)
            FAILF("page %#lx is mapped but lies in no usable or ACPI region", (unsigned long)ps);
    }
    KT_CHECK_EQ(D0.n, expected);
    /* The reserved, NVS and framebuffer ranges, and the hole, are not in the direct map. */
    KT_CHECK_EQ(dump_find(&D0, MI_DIRECT_MAP_BASE + 0xA0000), 0);
    KT_CHECK_EQ(dump_find(&D0, MI_DIRECT_MAP_BASE + 0x1010000), 0);
    KT_CHECK_EQ(dump_find(&D0, MI_DIRECT_MAP_BASE + 0x1000000) != 0, 1); /* ACPI reclaimable */
    PHYSICAL_ADDRESS pa;
    KT_CHECK_EQ(MiTranslate(S, MI_DIRECT_MAP_BASE + 0x300123, &pa, NULL), STATUS_SUCCESS);
    KT_CHECK_EQ(pa, 0x300123);
    KT_CHECK_EQ(MmVirtualToPhysical((void *)(uintptr_t)(MI_DIRECT_MAP_BASE + 0xABC000)), 0xABC000);
    KT_CHECK_EQ((uintptr_t)MmPhysicalToVirtual(0x1234), MI_DIRECT_MAP_BASE + 0x1234);
    KT_CHECK(MmPhysicalToVirtual(MI_DIRECT_MAP_LAST - MI_DIRECT_MAP_BASE + 1) == NULL);
    finish_checks();
    teardown();
}

static void test_lock_ranks_in_page_table_operations(void)
{
    setup(12);
    PHYSICAL_ADDRESS fa = uframe();
    memset(g_locks.acquired, 0, sizeof g_locks.acquired);
    g_locks.total = 0;
    KT_REQUIRE_EQ(MiMapPages(S, 0x400000, fa, 1, R | U), STATUS_SUCCESS);
    KT_CHECK_EQ(g_locks.acquired[LOCK_RANK_ADDRESS_SPACE], 1);
    KT_CHECK_EQ(g_locks.acquired[LOCK_RANK_FRAME_DB], 3); /* PDPT, PD, PT */
    KT_CHECK_EQ(g_locks.acquired[LOCK_RANK_POOL], 0);
    KT_CHECK_EQ(g_locks.total, 4);
    KT_CHECK(g_locks.last[LOCK_RANK_ADDRESS_SPACE] ==
             &S->Lock); /* a user address locks its own space */
    KT_REQUIRE_EQ(MiMapPages(S, MI_POOL_BASE, 0x300000, 1, R), STATUS_SUCCESS);
    KT_CHECK(g_locks.last[LOCK_RANK_ADDRESS_SPACE] ==
             &M.mm.KernelSpace.Lock); /* kernel half: the kernel space */
    KT_CHECK_EQ(MiUnmapPages(S, MI_POOL_BASE, 1), STATUS_SUCCESS);
    KT_CHECK(g_locks.last[LOCK_RANK_ADDRESS_SPACE] == &M.mm.KernelSpace.Lock);
    KT_CHECK_EQ(MiUnmapPages(S, 0x400000, 1), STATUS_SUCCESS);
    KT_CHECK(g_locks.last[LOCK_RANK_ADDRESS_SPACE] == &S->Lock);
    /* Failed calls take and release the same locks. */
    KT_CHECK_EQ(MiUnmapPages(S, 0x400000, 1), STATUS_INVALID_PARAMETER);
    (void)MiDereferenceFrame(&M.db, fa, MmFrameActive);
    finish_checks();
    teardown();
}

int main(void)
{
    static const kt_case cases[] = {
        KT_CASE("mm.pagetable", test_pte_layout),
        KT_CASE("mm.pagetable", test_rules_rejected),
        KT_CASE("mm.pagetable", test_map_translate_unmap_and_tlb),
        KT_CASE("mm.pagetable", test_protect_keeps_hardware_bits),
        KT_CASE("mm.pagetable", test_effective_rights_and_large_pages),
        KT_CASE("mm.pagetable", test_fault_injection_map),
        KT_CASE("mm.pagetable", test_address_space_creation_failures),
        KT_CASE("mm.pagetable", test_direct_map),
        KT_CASE("mm.pagetable", test_lock_ranks_in_page_table_operations),
    };
    return kt_run(cases, KAMOS_ARRAY_SIZE(cases)) ? 1 : 0;
}

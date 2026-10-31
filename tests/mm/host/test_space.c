/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Address spaces, the public mm.h wrappers and the user-range check. Owner: A04.
 * Oracles: the independent walker (mm_dump.h), frame and pool counters, a 128-bit arithmetic
 * model of the user range.
 */
#define KTEST_HOST 1
#include <ktest.h>

#include <stdio.h>

#include "mm_dump.h"

#define R MM_PROT_READ
#define W MM_PROT_WRITE
#define X MM_PROT_EXECUTE
#define U MM_PROT_USER

static HostMachine M;
static Dump        D0;
static char        g_msg[160];

#define FAILF(...)                                                                                 \
    do {                                                                                           \
        snprintf(g_msg, sizeof g_msg, __VA_ARGS__);                                                \
        KT_FAIL(g_msg);                                                                            \
    } while (0)

static void setup(void)
{
    HostMachineSmall(&M, 12);
    HostInitMm(&M, 1);
}

static void finish_checks(void)
{
    KT_CHECK_EQ(g_locks.violations, 0);
    KT_CHECK_EQ(g_locks.depth, 0);
    KT_CHECK_EQ(g_simWild, 0);
}

static PHYSICAL_ADDRESS uframe(void)
{
    PHYSICAL_ADDRESS pa;
    if (!NT_SUCCESS(MiAllocateFrame(&M.db, MmFrameActive, true, &pa)))
        abort();
    return pa;
}

static int kernel_half_equal(uint64_t rootA, uint64_t rootB)
{
    return memcmp(&dump_tbl(rootA)[256], &dump_tbl(rootB)[256], 256 * 8) == 0;
}

static void test_kernel_half_shared(void)
{
    setup();
    ADDRESS_SPACE *s[3];
    for (int i = 0; i < 3; i++)
        KT_REQUIRE_EQ(MiCreateAddressSpace(&M.mm, &s[i]), STATUS_SUCCESS);
    const uint64_t kroot = M.mm.KernelSpace.Root;
    for (int i = 0; i < 3; i++) {
        KT_CHECK(kernel_half_equal(kroot, s[i]->Root));
        for (unsigned k = 0; k < 256; k++)
            if (dump_tbl(s[i]->Root)[k] != 0)
                FAILF("space %d has a user PML4 entry %u at creation", i, k);
        /* every kernel PML4 entry has its PDPT from the start (no lazy copy needed later) */
        for (unsigned k = 256; k < 512; k++)
            if (dump_tbl(kroot)[k] != ((dump_tbl(kroot)[k] & X86O_ADDR_MASK) | X86O_P | X86O_RW))
                FAILF("kernel PML4 entry %u is not a plain present table", k);
    }
    uint64_t before[256];
    memcpy(before, &dump_tbl(kroot)[256], sizeof before);

    PHYSICAL_ADDRESS u = uframe();
    KT_REQUIRE_EQ(MiMapPages(s[0], MI_POOL_BASE + 0x5000, 0x300000, 3, R | W), STATUS_SUCCESS);
    KT_REQUIRE_EQ(MiMapPages(s[1], MI_DEVICE_BASE, 0x400000, 1, R | W), STATUS_SUCCESS);
    KT_REQUIRE_EQ(MiMapPages(s[2], MI_IMAGE_BASE, 0x200000, 2, R | X), STATUS_SUCCESS);
    KT_REQUIRE_EQ(MiMapPages(s[0], 0x600000, u, 1, R | W | U), STATUS_SUCCESS);
    for (int i = 0; i < 3; i++) {
        KT_CHECK_EQ(oracle_leaf(s[i]->Root, MI_POOL_BASE + 0x6000), x86o_pte(0x301000, 1, 0, 0));
        KT_CHECK_EQ(oracle_leaf(s[i]->Root, MI_DEVICE_BASE), x86o_pte(0x400000, 1, 0, 0));
        KT_CHECK_EQ(oracle_leaf(s[i]->Root, MI_IMAGE_BASE + 0x1000), x86o_pte(0x201000, 0, 1, 0));
    }
    KT_CHECK_EQ(oracle_leaf(kroot, MI_POOL_BASE + 0x5000), x86o_pte(0x300000, 1, 0, 0));
    /* The user mapping belongs to one space only. */
    KT_CHECK_EQ(oracle_leaf(s[0]->Root, 0x600000), x86o_pte(u, 1, 0, 1));
    KT_CHECK_EQ(oracle_leaf(s[1]->Root, 0x600000), 0);
    KT_CHECK_EQ(oracle_leaf(s[2]->Root, 0x600000), 0);
    KT_CHECK_EQ(oracle_leaf(kroot, 0x600000), 0);
    /* The kernel half of the PML4s did not move while the shared tables below it changed. */
    KT_CHECK(memcmp(before, &dump_tbl(kroot)[256], sizeof before) == 0);
    for (int i = 0; i < 3; i++)
        KT_CHECK(kernel_half_equal(kroot, s[i]->Root));

    KT_CHECK_EQ(MiUnmapPages(s[0], MI_POOL_BASE + 0x5000, 3), STATUS_SUCCESS);
    KT_CHECK_EQ(oracle_leaf(s[1]->Root, MI_POOL_BASE + 0x5000), 0); /* gone for everyone */
    /* Mapped through s[1], visible and removable through s[2]: the half is shared. */
    KT_CHECK_EQ(MiUnmapPages(s[2], MI_DEVICE_BASE, 1), STATUS_SUCCESS);
    KT_CHECK_EQ(oracle_leaf(s[0]->Root, MI_DEVICE_BASE), 0);
    KT_CHECK_EQ(MiUnmapPages(s[0], MI_DEVICE_BASE, 1), STATUS_INVALID_PARAMETER);
    KT_CHECK_EQ(MiUnmapPages(s[1], MI_IMAGE_BASE, 2), STATUS_SUCCESS);
    KT_CHECK_EQ(MiUnmapPages(s[0], 0x600000, 1), STATUS_SUCCESS);
    (void)MiDereferenceFrame(&M.db, u, MmFrameActive);
    for (int i = 0; i < 3; i++)
        MiDestroyAddressSpace(s[i]);
    KT_CHECK_EQ(M.mm.Pool.Live, 0);
    KT_CHECK_EQ(MiFrameDbVerify(&M.db), 0);
    finish_checks();
    HostTeardown(&M);
}

static void test_destroy_frees_tables_only(void)
{
    setup();
    ULONG64        tables0 = M.db.StateCount[MmFramePageTable];
    ULONG64        active0 = M.db.StateCount[MmFrameActive];
    ULONG64        live0   = M.mm.Pool.Live;
    ADDRESS_SPACE *s;
    KT_REQUIRE_EQ(MiCreateAddressSpace(&M.mm, &s), STATUS_SUCCESS);
    KT_CHECK_EQ(M.mm.Pool.Live, live0 + 1);
    PHYSICAL_ADDRESS      data[12];
    static const uint64_t vas[12] = {0x10000,      0x11000,      0x200000,     0x7FFFFFF000,
                                     0x8000000000, 0x8000001000, 0x4000000000, 0x7FFFFFFEF000,
                                     0x600000,     0x601000,     0x602000,     0x40000000};
    for (int i = 0; i < 12; i++) {
        data[i] = uframe();
        memset(g_hw.PhysicalToPointer(NULL, data[i], PAGE_SIZE), 0x40 + i, PAGE_SIZE);
        KT_REQUIRE_EQ(MiMapPages(s, vas[i], data[i], 1, R | W | U), STATUS_SUCCESS);
    }
    dump_space(&M.db, s->Root, 0, &D0);
    KT_CHECK_EQ(D0.n, 12);
    KT_CHECK_EQ(D0.defects, 0);
    ULONG64 tablesInSpace = D0.tables; /* user tables, root included */
    KT_CHECK_EQ(M.db.StateCount[MmFramePageTable], tables0 + tablesInSpace);

    /* The leaves are still mapped when the space is destroyed: the tables go, the frames stay. */
    MiDestroyAddressSpace(s);
    KT_CHECK_EQ(M.db.StateCount[MmFramePageTable], tables0);
    KT_CHECK_EQ(M.db.StateCount[MmFrameActive], active0 + 12);
    KT_CHECK_EQ(M.mm.Pool.Live, live0);
    for (int i = 0; i < 12; i++) {
        const uint8_t *p = g_hw.PhysicalToPointer(NULL, data[i], PAGE_SIZE);
        for (unsigned b = 0; b < PAGE_SIZE; b++)
            if (p[b] != 0x40 + i)
                FAILF("data frame %d changed by the destruction", i);
        KT_CHECK_EQ(MiGetFrame(&M.db, data[i])->State, MmFrameActive);
        (void)MiDereferenceFrame(&M.db, data[i], MmFrameActive);
    }
    KT_CHECK_EQ(MiFrameDbVerify(&M.db), 0);
    KT_CHECK_EQ(HostCheckFrames(&M), 0);
    finish_checks();
    HostTeardown(&M);
}

static void test_destroy_refusals(void)
{
    setup();
    ADDRESS_SPACE *s;
    KT_REQUIRE_EQ(MiCreateAddressSpace(&M.mm, &s), STATUS_SUCCESS);
    HostSnap snap;
    HostSnapTake(&M.db, &snap);
    int stopped;

    HOST_CATCH(stopped, MiDestroyAddressSpace(&M.mm.KernelSpace));
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.p[0], MM_STOP_BASE | MM_STOP_SPACE_DESTROY_KERNEL);
    KT_CHECK(HostSnapSame(&M.db, &snap));

    MiSwitchAddressSpace(s);
    HOST_CATCH(stopped, MiDestroyAddressSpace(s));
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.p[0], MM_STOP_BASE | MM_STOP_SPACE_DESTROY_CURRENT);
    KT_CHECK(HostSnapSame(&M.db, &snap));
    MiSwitchAddressSpace(&M.mm.KernelSpace);
    g_hostRoot = s->Root; /* loaded on a processor although the software pointer is elsewhere */
    HOST_CATCH(stopped, MiDestroyAddressSpace(s));
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.p[0], MM_STOP_BASE | MM_STOP_SPACE_DESTROY_CURRENT);
    g_hostRoot = M.mm.KernelSpace.Root;

    HOST_CATCH(stopped, MiDestroyAddressSpace(NULL));
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.p[0], MM_STOP_BASE | MM_STOP_SPACE_BAD);
    HOST_CATCH(stopped, MiMapPages(NULL, 0x400000, 0x300000, 1, R | U));
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.p[0], MM_STOP_BASE | MM_STOP_SPACE_BAD);
    HOST_CATCH(stopped, MiSwitchAddressSpace(NULL));
    KT_CHECK_EQ(stopped, 1);

    MiDestroyAddressSpace(s);
    HOST_CATCH(stopped, MiDestroyAddressSpace(s)); /* second destruction of the same object */
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.p[0], MM_STOP_BASE | MM_STOP_SPACE_BAD);
    HostSnapFree(&snap);
    finish_checks();
    HostTeardown(&M);
}

static void test_kernel_space_adopts_boot_tables(void)
{
    /* A loader built a PML4 with one kernel entry already present: it is kept, the empty slots
     * get a PDPT. A failure part-way leaves nothing behind. */
    HostMachineSmall(&M, 12);
    HostInitMm(&M, 1);
    MM_CONTEXT second;
    MiContextInit(&second, &g_hw, &M.db, 40);
    second.TableFrames = M.mm.TableFrames; /* the fault-injecting source of the machine */
    /* Build the "boot" tables by hand: a root with entry 511 -> a PDPT that has one entry. */
    PHYSICAL_ADDRESS root, pdpt;
    KT_REQUIRE_EQ(MiAllocateFrame(&M.db, MmFramePageTable, true, &root), STATUS_SUCCESS);
    KT_REQUIRE_EQ(MiAllocateFrame(&M.db, MmFramePageTable, true, &pdpt), STATUS_SUCCESS);
    dump_tbl(root)[511]  = pdpt | X86O_P | X86O_RW;
    dump_tbl(pdpt)[510]  = 0x40000000 | X86O_P | X86O_RW | (UINT64_C(1) << 7); /* a 1 GiB page */
    ULONG64 tablesBefore = M.db.StateCount[MmFramePageTable];

    /* Allocation of the 255 missing PDPTs fails at some point: roll back. */
    HostSnap snap;
    HostSnapTake(&M.db, &snap);
    uint64_t saved511 = dump_tbl(root)[511];
    for (long k = 0; k < 255; k += 37) {
        /* a context whose frame source fails after k allocations */
        g_tableFailAfter = k;
        KT_CHECK_EQ(MiInitKernelSpace(&second, root), STATUS_NO_MEMORY);
        g_tableFailAfter = -1;
        KT_CHECK(HostSnapSame(&M.db, &snap));
        for (unsigned i = 256; i < 511; i++)
            if (dump_tbl(root)[i] != 0)
                FAILF("after failure %ld: PML4 entry %u left behind", k, i);
        KT_CHECK_EQ(dump_tbl(root)[511], saved511);
    }
    KT_CHECK_EQ(MiInitKernelSpace(&second, root), STATUS_SUCCESS);
    KT_CHECK_EQ(M.db.StateCount[MmFramePageTable], tablesBefore + 255);
    KT_CHECK_EQ(dump_tbl(root)[511], saved511);
    KT_CHECK_EQ(dump_tbl(pdpt)[510], 0x40000000 | X86O_P | X86O_RW | (UINT64_C(1) << 7));
    for (unsigned i = 256; i < 511; i++)
        KT_CHECK_EQ(dump_tbl(root)[i], (dump_tbl(root)[i] & X86O_ADDR_MASK) | X86O_P | X86O_RW);
    /* The 1 GiB page of the loader is translated, not touched. */
    PHYSICAL_ADDRESS pa;
    KT_CHECK_EQ(MiTranslate(&second.KernelSpace, UINT64_C(0xFFFFFFFF80000000) + 0x1234, &pa, NULL),
                STATUS_SUCCESS);
    KT_CHECK_EQ(pa, 0x40000000 + 0x1234);
    HostSnapFree(&snap);
    finish_checks();
    HostTeardown(&M);
}

static void test_public_wrappers(void)
{
    setup();
    int stopped;
    /* Before the first switch there is no current space. */
    HOST_CATCH(stopped, (void)MmMapPages((void *)(uintptr_t)0x400000, 0x300000, 1, R | U));
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.p[0], MM_STOP_BASE | MM_STOP_SPACE_BAD);

    KT_CHECK(MmGetKernelAddressSpace() == &M.mm.KernelSpace);
    ADDRESS_SPACE *s;
    KT_REQUIRE_EQ(MmCreateAddressSpace(&s), STATUS_SUCCESS);
    MmSwitchAddressSpace(s);
    KT_CHECK(MmGetCurrentAddressSpace() == s);
    KT_CHECK_EQ(g_hostRoot, s->Root);

    PHYSICAL_ADDRESS page;
    KT_REQUIRE_EQ(MmAllocatePhysicalPage(&page), STATUS_SUCCESS);
    KT_CHECK_EQ(MiGetFrame(&M.db, page)->State, MmFrameActive);
    const uint64_t *w = g_hw.PhysicalToPointer(NULL, page, PAGE_SIZE);
    for (unsigned i = 0; i < PAGE_SIZE / 8; i++)
        KT_CHECK_EQ(w[i], 0);
    KT_CHECK_EQ(MmAllocatePhysicalPage(NULL), STATUS_INVALID_PARAMETER);

    void *va = (void *)(uintptr_t)0x500000;
    KT_REQUIRE_EQ(MmMapPages(va, page, 1, R | W | U), STATUS_SUCCESS);
    KT_CHECK_EQ(oracle_leaf(s->Root, 0x500000), x86o_pte(page, 1, 0, 1));
    KT_CHECK_EQ(MmVirtualToPhysical((const char *)va + 0x10), page + 0x10);
    KT_CHECK_EQ(MmMapPages(va, page, 1, R | W | U), STATUS_OBJECT_NAME_COLLISION);
    KT_CHECK_EQ(MmProtectPages(va, 1, R | U), STATUS_SUCCESS);
    KT_CHECK_EQ(oracle_leaf(s->Root, 0x500000), x86o_pte(page, 0, 0, 1));
    KT_CHECK_EQ(MmUnmapPages(va, 1), STATUS_SUCCESS);
    KT_CHECK_EQ(MmVirtualToPhysical(va), ~(PHYSICAL_ADDRESS)0); /* not mapped: all ones */
    KT_CHECK_EQ(MmVirtualToPhysical((void *)(uintptr_t)(MI_DIRECT_MAP_BASE + 0x1234000)),
                0x1234000);

    /* The InSpace forms act on a space that is not current. */
    ADDRESS_SPACE *t;
    KT_REQUIRE_EQ(MmCreateAddressSpace(&t), STATUS_SUCCESS);
    KT_REQUIRE_EQ(MmMapPagesInSpace(t, va, page, 1, R | U), STATUS_SUCCESS);
    KT_CHECK_EQ(oracle_leaf(t->Root, 0x500000), x86o_pte(page, 0, 0, 1));
    KT_CHECK_EQ(oracle_leaf(s->Root, 0x500000), 0);
    KT_CHECK_EQ(MmUnmapPagesInSpace(t, va, 1), STATUS_SUCCESS);

    MmFreePhysicalPage(page);
    HostSnap snap;
    HostSnapTake(&M.db, &snap);
    HOST_CATCH(stopped, MmFreePhysicalPage(page)); /* double free */
    KT_CHECK_EQ(stopped, 1);
    KT_CHECK_EQ(g_stop.p[0], MM_STOP_BASE | MM_STOP_FRAME_DOUBLE_FREE);
    KT_CHECK(HostSnapSame(&M.db, &snap));
    HostSnapFree(&snap);

    void *b = MmAllocatePool(NonPagedPool, 100, 0x74736554u /* 'Test' */);
    KT_REQUIRE(b != NULL);
    MmFreePool(b, 0x74736554u);
    MmSwitchAddressSpace(MmGetKernelAddressSpace());
    MmDestroyAddressSpace(s);
    MmDestroyAddressSpace(t);
    finish_checks();
    HostTeardown(&M);
}

static void test_user_range_check(void)
{
    static const struct {
        uint64_t addr, len;
        int      ok;
    } t[] = {
        {0x10000, 1, 1},
        {0xFFFF, 1, 0},
        {0, 1, 0},
        {0x7FFFFFFEFFFF, 1, 1},
        {0x7FFFFFFEFFFF, 2, 0},
        {0x7FFFFFFF0000, 1, 0},
        {0x10000, 0x7FFFFFFE0000, 1},
        {0x10000, 0x7FFFFFFE0001, 0},
        {0x10000, UINT64_MAX, 0},
        {UINT64_MAX, 1, 0},
        {UINT64_C(0xFFFF800000000000), 1, 0},
        {UINT64_C(0xFFFFFFFFFFFFF000), 0x2000, 0},
        {UINT64_C(0x0000800000000000), 1, 0},
        {0x500000, 0, 1}, /* nothing is touched */
        {0, 0, 1},
    };
    for (unsigned i = 0; i < KAMOS_ARRAY_SIZE(t); i++)
        if (MiIsUserRange(t[i].addr, t[i].len) != t[i].ok)
            FAILF("range case %u (%#lx, %#lx)", i, (unsigned long)t[i].addr,
                  (unsigned long)t[i].len);
    HostRng rng = {0x1234567};
    for (unsigned i = 0; i < 200000; i++) {
        uint64_t a, l;
        switch (HostRandBelow(&rng, 4)) {
        case 0:
            a = HostRand(&rng);
            break;
        case 1:
            a = 0x7FFFFFFF0000ull - HostRandBelow(&rng, 0x20000);
            break;
        case 2:
            a = 0x10000 - 0x100 + HostRandBelow(&rng, 0x200);
            break;
        default:
            a = HostRandBelow(&rng, UINT64_C(1) << 47);
            break;
        }
        l        = (HostRand(&rng) & 1) ? HostRandBelow(&rng, 0x40000)
                                        : HostRand(&rng) >> HostRandBelow(&rng, 64);
        int want = (l == 0) || ((a >= 0x10000) && ((unsigned __int128)a + l <= 0x7FFFFFFF0000ull));
        if (MiIsUserRange(a, l) != want)
            FAILF("random range (%#lx, %#lx): got %d", (unsigned long)a, (unsigned long)l, !want);
    }
}

int main(void)
{
    static const kt_case cases[] = {
        KT_CASE("mm.space", test_kernel_half_shared),
        KT_CASE("mm.space", test_destroy_frees_tables_only),
        KT_CASE("mm.space", test_destroy_refusals),
        KT_CASE("mm.space", test_kernel_space_adopts_boot_tables),
        KT_CASE("mm.space", test_public_wrappers),
        KT_CASE("mm.space", test_user_range_check),
    };
    return kt_run(cases, KAMOS_ARRAY_SIZE(cases)) ? 1 : 0;
}

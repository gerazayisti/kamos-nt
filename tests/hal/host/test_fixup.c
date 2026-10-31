/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Host tests of the fixup lookup (arch/x86_64/interrupts/fixup.c, ADR-0013). The policy around the
 * lookup (PASSIVE_LEVEL only, #PF only) is tested in the kernel (suite pfpolicy).
 */
#define KTEST_HOST 1
#include <ktest.h>

#include "../../../arch/x86_64/interrupts/fixup.h"

static const KFIXUP_ENTRY Table[] = {
    {0xffffffff80100100ull, 0xffffffff80100200ull},
    {0xffffffff80100300ull, 0xffffffff80100400ull},
    {0xffffffff80100500ull, 0xffffffff80100600ull},
};
#define N (sizeof Table / sizeof Table[0])

static void test_exact_match(void)
{
    for (size_t i = 0; i < N; i++) {
        const KFIXUP_ENTRY *e = KiFindFixup(Table, N, Table[i].FaultRip);
        KT_REQUIRE(e == &Table[i]);
        KT_CHECK_EQ(e->ResumeRip, Table[i].ResumeRip);
    }
}

static void test_near_misses(void)
{
    /* One byte off in either direction, the resume address, and the null/extreme addresses. */
    for (size_t i = 0; i < N; i++) {
        KT_CHECK(KiFindFixup(Table, N, Table[i].FaultRip - 1) == NULL);
        KT_CHECK(KiFindFixup(Table, N, Table[i].FaultRip + 1) == NULL);
        KT_CHECK(KiFindFixup(Table, N, Table[i].ResumeRip) == NULL);
    }
    KT_CHECK(KiFindFixup(Table, N, 0) == NULL);
    KT_CHECK(KiFindFixup(Table, N, ~0ull) == NULL);
}

static void test_empty(void)
{
    KT_CHECK(KiFindFixup(Table, 0, Table[0].FaultRip) == NULL);
    KT_CHECK(KiFindFixup(NULL, 0, 0) == NULL);
}

static void test_count_is_respected(void)
{
    /* The entry after Count must not be found. */
    KT_CHECK(KiFindFixup(Table, 2, Table[2].FaultRip) == NULL);
    KT_CHECK(KiFindFixup(Table, 3, Table[2].FaultRip) == &Table[2]);
}

int main(void)
{
    static const kt_case cases[] = {
        KT_CASE("fixup", test_exact_match),
        KT_CASE("fixup", test_near_misses),
        KT_CASE("fixup", test_empty),
        KT_CASE("fixup", test_count_is_respected),
    };
    return kt_run(cases, sizeof cases / sizeof cases[0]) ? 1 : 0;
}

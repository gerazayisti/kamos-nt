/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* The framework tests itself: a mix of macros, all expected to pass. Owner: A10. */
#define KTEST_HOST 1
#include <ktest.h>
#include <x86_oracle.h>

static void test_check_true(void) { KT_CHECK(1 + 1 == 2); }
static void test_check_eq(void) { KT_CHECK_EQ(0x1000u + 0x234u, 0x1234u); }
static void test_require_returns(void)
{
    KT_REQUIRE(1);
    KT_REQUIRE_EQ(7, 7);
}
static void test_skip_with_reason(void) { KT_SKIP("deliberate skip, declared in the manifest"); }
static void test_pte_oracle(void)
{
    /* Hand-computed from the SDM layout: P|RW|US = 0x7, frame 0x5000, executable. */
    KT_CHECK_EQ(x86o_pte(0x5000, 1, 1, 1), 0x5007u);
    /* Read-only, non-executable, supervisor: P | XD. */
    KT_CHECK_EQ(x86o_pte(0x5000, 0, 0, 0), UINT64_C(0x8000000000005001));
    KT_CHECK(x86o_canonical48(0x00007FFFFFFFFFFFull));
    KT_CHECK(!x86o_canonical48(0x0000800000000000ull));
    KT_CHECK(x86o_canonical48(0xFFFF800000000000ull));
    KT_CHECK_EQ(x86o_index(0xFFFFFFFF80100000ull, 4), 511u);
    KT_CHECK_EQ(x86o_index(0xFFFFFFFF80100000ull, 2), 0u);
    KT_CHECK_EQ(x86o_index(0xFFFFFFFF80100000ull, 1), 0x100u);
}

int main(void)
{
    static const kt_case cases[] = {
        KT_CASE("ktest", test_check_true),
        KT_CASE("ktest", test_check_eq),
        KT_CASE("ktest", test_require_returns),
        KT_CASE("ktest", test_skip_with_reason),
        KT_CASE("ktest", test_pte_oracle),
    };
    return kt_run(cases, sizeof cases / sizeof cases[0]) ? 1 : 0;
}

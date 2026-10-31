/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* The framework must report these failures: the harness runs this program and requires exit 1,
 * one FAIL line per failing test and the first-failure detail. Owner: A10. */
#define KTEST_HOST 1
#include <ktest.h>

static void test_check_fails(void) { KT_CHECK(1 == 2); }
static void test_eq_fails(void) { KT_CHECK_EQ(3, 4); }
static void test_require_stops(void)
{
    KT_REQUIRE(0);
    KT_CHECK(1);   /* never reached */
}
static void test_passes(void) { KT_CHECK(1); }

int main(void)
{
    static const kt_case cases[] = {
        KT_CASE("neg", test_check_fails),
        KT_CASE("neg", test_eq_fails),
        KT_CASE("neg", test_require_stops),
        KT_CASE("neg", test_passes),
    };
    return kt_run(cases, sizeof cases / sizeof cases[0]) ? 1 : 0;
}

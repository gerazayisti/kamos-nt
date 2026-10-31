/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Compiled with the kernel flags (ADR-0003) to prove ktest.h is freestanding: the only undefined
 * symbol of the object must be kt_sink. Not linked into anything. Owner: A10. */
#include <ktest.h>

static void t(void) { KT_CHECK_EQ(1, 1); }

unsigned ktest_freestanding_probe(void)
{
    static const kt_case cases[] = { KT_CASE("probe", t) };
    return kt_run(cases, 1);
}

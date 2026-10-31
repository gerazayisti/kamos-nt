/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Planted defects: tools/ci/static-analysis.sh --selftest requires cppcheck to report them. */
#include <stddef.h>

int null_dereference(int flag)
{
    int *p = NULL;
    if (flag) {
        p = NULL;
    }
    return *p;
}

void out_of_bounds(void)
{
    int a[4];
    a[4] = 1;
}

int uninitialised(void)
{
    int x;
    return x + 1;
}

/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* No defect: tools/ci/static-analysis.sh --selftest requires cppcheck to stay silent. */
#include <stddef.h>

int sum(const int *values, size_t count)
{
    int total = 0;
    for (size_t i = 0; i < count; i++) {
        total += values[i];
    }
    return total;
}

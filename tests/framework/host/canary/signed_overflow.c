/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Canary: UBSan (no recovery) must kill this program (signed integer overflow). Owner: A10. */
#include <limits.h>
int main(void)
{
    volatile int x = INT_MAX;
    int y = x + 1;
    return y == 0;
}

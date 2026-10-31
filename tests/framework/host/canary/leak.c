/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Canary: LeakSanitizer must report this program (detected memory leaks). Owner: A10. */
#include <stdlib.h>
int main(void)
{
    volatile char *p = malloc(8);
    p[0] = 1;
    p = 0;
    return 0;
}

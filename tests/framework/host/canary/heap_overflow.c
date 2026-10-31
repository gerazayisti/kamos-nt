/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Canary: AddressSanitizer must kill this program (heap-buffer-overflow). Owner: A10. */
#include <stdlib.h>
int main(void)
{
    volatile int idx = 4;
    char *p = malloc(4);
    p[idx] = 1;
    int r = p[0];
    free(p);
    return r;
}

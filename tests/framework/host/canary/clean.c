/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Positive control: a correct program must pass untouched, so the harness is not killing
 * everything. Owner: A10. */
#include <stdlib.h>
int main(void)
{
    char *p = malloc(8);
    if (!p)
        return 1;
    p[7] = 1;
    free(p);
    return 0;
}

/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Byte-wise string and memory routines. GCC may call these by name even in freestanding code. */

#include "rtl.h"

/* The loops below must not be turned back into calls to themselves. */
#pragma GCC optimize("no-tree-loop-distribute-patterns")

void *memcpy(void *Dest, const void *Src, size_t Count)
{
    uint8_t       *d = Dest;
    const uint8_t *s = Src;

    while (Count-- != 0) {
        *d++ = *s++;
    }
    return Dest;
}

void *memmove(void *Dest, const void *Src, size_t Count)
{
    uint8_t       *d = Dest;
    const uint8_t *s = Src;

    if (d == s || Count == 0) {
        return Dest;
    }
    if (d < s) {
        while (Count-- != 0) {
            *d++ = *s++;
        }
    } else {
        d += Count;
        s += Count;
        while (Count-- != 0) {
            *--d = *--s;
        }
    }
    return Dest;
}

void *memset(void *Dest, int Value, size_t Count)
{
    uint8_t *d = Dest;

    while (Count-- != 0) {
        *d++ = (uint8_t)Value;
    }
    return Dest;
}

int memcmp(const void *A, const void *B, size_t Count)
{
    const uint8_t *a = A;
    const uint8_t *b = B;

    while (Count-- != 0) {
        if (*a != *b) {
            return (int)*a - (int)*b;
        }
        a++;
        b++;
    }
    return 0;
}

size_t strlen(const char *Str)
{
    size_t n = 0;

    while (Str[n] != '\0') {
        n++;
    }
    return n;
}

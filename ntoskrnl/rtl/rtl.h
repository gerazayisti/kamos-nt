/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Minimum run-time library (A03 until the Wave 1 gate, then A05). Nothing here allocates.
 * The formatter is pure and also builds on the host (tests/hal/host).
 */
#ifndef KAMOS_RTL_H
#define KAMOS_RTL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifndef KAMOS_RTL_HOST
void  *memcpy(void *Dest, const void *Src, size_t Count);
void  *memmove(void *Dest, const void *Src, size_t Count);
void  *memset(void *Dest, int Value, size_t Count);
int    memcmp(const void *A, const void *B, size_t Count);
size_t strlen(const char *Str);
#endif

/*
 * Formats into Buffer (capacity Capacity, always NUL-terminated when Capacity > 0) and returns the
 * length the full output would have (like snprintf), so truncation is detectable.
 * Supported: %c %s %d %i %u %x %X %p %%, flags - 0 #, width (digits or *), length l ll z.
 * %p prints 0x and 16 hex digits. A null %s prints (null). Unknown conversions are copied.
 */
size_t RtlFormatV(char *Buffer, size_t Capacity, const char *Format, va_list Args);
size_t RtlFormat(char *Buffer, size_t Capacity, const char *Format, ...)
    __attribute__((format(printf, 3, 4)));

#endif /* KAMOS_RTL_H */

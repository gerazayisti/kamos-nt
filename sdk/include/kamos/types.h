/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Base types shared by every kernel subsystem.
 * Owner: A01. Contract state: provisional (ADR-0007, ADR-0008).
 * Freestanding: only <stdint.h>, <stddef.h> and <stdbool.h> are used.
 */
#ifndef KAMOS_TYPES_H
#define KAMOS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t   UCHAR;
typedef uint16_t  USHORT;
typedef uint32_t  ULONG;
typedef int32_t   LONG;
typedef uint64_t  ULONG64;
typedef int64_t   LONG64;
typedef uintptr_t ULONG_PTR;
typedef size_t    SIZE_T;
typedef bool      BOOLEAN;
typedef uint16_t  WCHAR;

typedef uint64_t  PHYSICAL_ADDRESS;
typedef ULONG     ACCESS_MASK;
typedef ULONG64   HANDLE;           /* Opaque: index + generation, never a pointer (ADR-0011). */

/* Counted UTF-16 string, as documented for PE-visible APIs. Length in bytes. */
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    WCHAR *Buffer;
} UNICODE_STRING;

/* Doubly linked list entry (Rtl list helpers live in ntoskrnl/rtl/). */
typedef struct _LIST_ENTRY {
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
} LIST_ENTRY;

#define KAMOS_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define KAMOS_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

#endif /* KAMOS_TYPES_H */

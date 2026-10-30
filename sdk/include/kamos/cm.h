/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Configuration store (registry). On-disk format is a Kamos-NT design.
 * Owner: A08. Contract state: provisional until the Wave 4 gate.
 */
#ifndef KAMOS_CM_H
#define KAMOS_CM_H

#include <kamos/status.h>
#include <kamos/types.h>

typedef enum _CM_VALUE_TYPE {
    CmValueNone = 0,
    CmValueString = 1,     /* UTF-16, NUL-terminated. */
    CmValueBinary = 3,
    CmValueUlong = 4,
    CmValueUlong64 = 11,
} CM_VALUE_TYPE;

/* All functions: PASSIVE_LEVEL. Paths use backslashes and are case-insensitive. */
NTSTATUS CmCreateKey(const UNICODE_STRING *Path);
NTSTATUS CmDeleteKey(const UNICODE_STRING *Path);
NTSTATUS CmSetValue(const UNICODE_STRING *KeyPath, const UNICODE_STRING *ValueName,
                    CM_VALUE_TYPE Type, const void *Data, ULONG DataSize);
/* STATUS_BUFFER_TOO_SMALL sets *DataSize to the required size. */
NTSTATUS CmQueryValue(const UNICODE_STRING *KeyPath, const UNICODE_STRING *ValueName,
                      CM_VALUE_TYPE *Type, void *Data, ULONG *DataSize);
NTSTATUS CmFlush(void);

#endif /* KAMOS_CM_H */

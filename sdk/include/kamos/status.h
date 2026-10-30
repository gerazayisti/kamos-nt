/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * NTSTATUS type and initial status values (ADR-0007).
 * Owner: A01. Contract state: provisional.
 * Values come from the public NTSTATUS reference ([MS-ERREF] section 2.3); add new ones
 * only with the same public source.
 */
#ifndef KAMOS_STATUS_H
#define KAMOS_STATUS_H

#include <stdint.h>

typedef int32_t NTSTATUS;

#define NT_SUCCESS(s)     ((NTSTATUS)(s) >= 0)
#define NT_INFORMATION(s) ((((uint32_t)(s)) >> 30) == 1)
#define NT_WARNING(s)     ((((uint32_t)(s)) >> 30) == 2)
#define NT_ERROR(s)       ((((uint32_t)(s)) >> 30) == 3)

#define KAMOS_STATUS(v) ((NTSTATUS)(uint32_t)(v))

#define STATUS_SUCCESS                  KAMOS_STATUS(0x00000000u)
#define STATUS_TIMEOUT                  KAMOS_STATUS(0x00000102u)
#define STATUS_PENDING                  KAMOS_STATUS(0x00000103u)
#define STATUS_UNSUCCESSFUL             KAMOS_STATUS(0xC0000001u)
#define STATUS_NOT_IMPLEMENTED          KAMOS_STATUS(0xC0000002u)
#define STATUS_ACCESS_VIOLATION         KAMOS_STATUS(0xC0000005u)
#define STATUS_INVALID_HANDLE           KAMOS_STATUS(0xC0000008u)
#define STATUS_INVALID_PARAMETER        KAMOS_STATUS(0xC000000Du)
#define STATUS_NO_MEMORY                KAMOS_STATUS(0xC0000017u)
#define STATUS_ACCESS_DENIED            KAMOS_STATUS(0xC0000022u)
#define STATUS_BUFFER_TOO_SMALL         KAMOS_STATUS(0xC0000023u)
#define STATUS_OBJECT_NAME_NOT_FOUND    KAMOS_STATUS(0xC0000034u)
#define STATUS_OBJECT_NAME_COLLISION    KAMOS_STATUS(0xC0000035u)
#define STATUS_INVALID_IMAGE_FORMAT     KAMOS_STATUS(0xC000007Bu)
#define STATUS_INSUFFICIENT_RESOURCES   KAMOS_STATUS(0xC000009Au)
#define STATUS_CANCELLED                KAMOS_STATUS(0xC0000120u)

/* Kamos-NT specific codes: customer bit set, FACILITY_KAMOS (ADR-0007). */
#define FACILITY_KAMOS 0x0A0u
#define KAMOS_ERROR_CODE(code) \
    KAMOS_STATUS(0xE0000000u | (FACILITY_KAMOS << 16) | ((code) & 0xFFFFu))

#endif /* KAMOS_STATUS_H */

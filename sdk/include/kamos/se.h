/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Security reference monitor.
 * Owner: A06. Contract state: provisional; frozen early because the object manager calls
 * SeAccessCheck before Phase 7 exists. Until Phase 7 the implementation grants every
 * request and is listed as Stubbed in docs/status/se.md.
 */
#ifndef KAMOS_SE_H
#define KAMOS_SE_H

#include <kamos/status.h>
#include <kamos/types.h>

/* Generic rights, mapped to object-specific rights by each object type. */
#define GENERIC_READ    0x80000000u
#define GENERIC_WRITE   0x40000000u
#define GENERIC_EXECUTE 0x20000000u
#define GENERIC_ALL     0x10000000u

typedef struct _ACCESS_TOKEN ACCESS_TOKEN;               /* Opaque. */
typedef struct _SECURITY_DESCRIPTOR SECURITY_DESCRIPTOR; /* Opaque. */

typedef struct _GENERIC_MAPPING {
    ACCESS_MASK GenericRead;
    ACCESS_MASK GenericWrite;
    ACCESS_MASK GenericExecute;
    ACCESS_MASK GenericAll;
} GENERIC_MAPPING;

/*
 * Token of the thread that is running. Never NULL. PASSIVE_LEVEL. Until Phase 7 it returns a
 * static system token (Stubbed). The object manager uses it for every access check, so no
 * caller passes a token of its own (RFC-0001).
 */
const ACCESS_TOKEN *SeGetCurrentToken(void);

/*
 * Decides whether Token may obtain DesiredAccess on an object protected by Descriptor.
 * A NULL Descriptor means the object is unprotected and the request is granted after mapping.
 * On success *GrantedAccess holds the granted rights. PASSIVE_LEVEL.
 * Errors: STATUS_ACCESS_DENIED, STATUS_INVALID_PARAMETER.
 */
NTSTATUS SeAccessCheck(const SECURITY_DESCRIPTOR *Descriptor, const ACCESS_TOKEN *Token,
                       ACCESS_MASK DesiredAccess, const GENERIC_MAPPING *Mapping,
                       ACCESS_MASK *GrantedAccess);

#endif /* KAMOS_SE_H */

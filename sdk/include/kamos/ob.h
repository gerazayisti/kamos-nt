/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Object manager: typed objects, references, handles, namespace.
 * Owner: A06. Contract state: provisional until the Wave 3 gate.
 *
 * Ownership rules: a reference owns an object; a handle owns one reference; deletion runs
 * exactly once, deferred out of DISPATCH_LEVEL when the last reference drops there.
 */
#ifndef KAMOS_OB_H
#define KAMOS_OB_H

#include <kamos/status.h>
#include <kamos/types.h>

typedef struct _OBJECT_TYPE OBJECT_TYPE;     /* Opaque. */
typedef struct _HANDLE_TABLE HANDLE_TABLE;   /* Opaque; one per process (A07 owns the process). */

typedef struct _OBJECT_TYPE_INITIALIZER {
    const char *Name;
    ACCESS_MASK ValidAccessMask;
    void (*DeleteProcedure)(void *Object);                 /* PASSIVE_LEVEL. */
    void (*CloseProcedure)(void *Object, ULONG HandleCount);
} OBJECT_TYPE_INITIALIZER;

/* PASSIVE_LEVEL. */
NTSTATUS ObCreateObjectType(const OBJECT_TYPE_INITIALIZER *Init, OBJECT_TYPE **Type);
NTSTATUS ObCreateObject(OBJECT_TYPE *Type, SIZE_T BodySize, const UNICODE_STRING *Name,
                        void **Object);

/* Up to DISPATCH_LEVEL. */
void ObReferenceObject(void *Object);
void ObDereferenceObject(void *Object);

/*
 * Handle operations. PASSIVE_LEVEL.
 * Errors: STATUS_INVALID_HANDLE (closed, forged, stale generation, wrong type),
 * STATUS_ACCESS_DENIED (DesiredAccess not granted), STATUS_INSUFFICIENT_RESOURCES.
 */
NTSTATUS ObInsertHandle(HANDLE_TABLE *Table, void *Object, ACCESS_MASK GrantedAccess,
                        HANDLE *Handle);
NTSTATUS ObReferenceObjectByHandle(HANDLE_TABLE *Table, HANDLE Handle,
                                   ACCESS_MASK DesiredAccess, OBJECT_TYPE *Type,
                                   void **Object);
NTSTATUS ObCloseHandle(HANDLE_TABLE *Table, HANDLE Handle);
NTSTATUS ObDuplicateHandle(HANDLE_TABLE *Source, HANDLE SourceHandle, HANDLE_TABLE *Target,
                           ACCESS_MASK DesiredAccess, HANDLE *TargetHandle);

/* Namespace lookup, case-insensitive and case-preserving (ADR-0011). PASSIVE_LEVEL. */
NTSTATUS ObReferenceObjectByName(const UNICODE_STRING *Path, OBJECT_TYPE *Type,
                                 void **Object);

#endif /* KAMOS_OB_H */

/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Object manager: typed objects, references, handles, namespace.
 * Owner: A06. Contract state: provisional until the Wave 3 gate (amended by RFC-0001).
 *
 * Ownership rules: a reference owns an object; a handle owns one reference; the namespace owns
 * one reference to each named object. Deletion runs exactly once, deferred to a work item
 * (ke.h) when the last reference drops above PASSIVE_LEVEL.
 *
 * Access control: every path that turns a name or a request into a granted access mask goes
 * through SeAccessCheck (se.h) with the current thread's token. Until Phase 7 the check grants
 * everything and is listed as Stubbed in docs/status/se.md; the signatures below do not change
 * when it becomes real.
 */
#ifndef KAMOS_OB_H
#define KAMOS_OB_H

#include <kamos/se.h>
#include <kamos/status.h>
#include <kamos/types.h>

typedef struct _OBJECT_TYPE OBJECT_TYPE;     /* Opaque. */
typedef struct _HANDLE_TABLE HANDLE_TABLE;   /* Opaque; one per process (A07 owns the process). */

typedef struct _OBJECT_TYPE_INITIALIZER {
    const char *Name;
    ACCESS_MASK ValidAccessMask;
    GENERIC_MAPPING GenericMapping;                        /* Generic to specific rights. */
    void (*DeleteProcedure)(void *Object);                 /* PASSIVE_LEVEL. */
    void (*CloseProcedure)(void *Object, ULONG HandleCount);
} OBJECT_TYPE_INITIALIZER;

/* PASSIVE_LEVEL. */
NTSTATUS ObCreateObjectType(const OBJECT_TYPE_INITIALIZER *Init, OBJECT_TYPE **Type);

/*
 * Creates an object with one reference, owned by the caller, and not visible in the namespace.
 * Descriptor is copied; NULL selects the default descriptor of the type.
 * Errors: STATUS_NO_MEMORY, STATUS_INVALID_PARAMETER. PASSIVE_LEVEL.
 */
NTSTATUS ObCreateObject(OBJECT_TYPE *Type, SIZE_T BodySize,
                        const SECURITY_DESCRIPTOR *Descriptor, void **Object);

/*
 * Makes an object visible under Path. The namespace takes its own reference.
 * Errors: STATUS_OBJECT_NAME_COLLISION, STATUS_INVALID_PARAMETER, STATUS_NO_MEMORY.
 * PASSIVE_LEVEL.
 */
NTSTATUS ObInsertObject(void *Object, const UNICODE_STRING *Path);

/* Up to DISPATCH_LEVEL. */
void ObReferenceObject(void *Object);
void ObDereferenceObject(void *Object);

/*
 * Handle tables. PASSIVE_LEVEL.
 * ObDestroyHandleTable closes every remaining handle. ObInheritHandles copies, into the empty
 * table Target, the handles of Source that carry OB_HANDLE_INHERIT, keeping their values and
 * taking one new reference each.
 */
#define OB_HANDLE_INHERIT 0x1u

NTSTATUS ObCreateHandleTable(HANDLE_TABLE **Table);
void     ObDestroyHandleTable(HANDLE_TABLE *Table);
NTSTATUS ObInheritHandles(HANDLE_TABLE *Source, HANDLE_TABLE *Target);

/*
 * Handle operations. PASSIVE_LEVEL.
 * Errors: STATUS_INVALID_HANDLE (closed, forged, stale generation, wrong type),
 * STATUS_ACCESS_DENIED (DesiredAccess not granted), STATUS_INSUFFICIENT_RESOURCES.
 * When two tables are locked together (duplicate), the lock with the lower address is taken
 * first (docs/architecture/lock-order.md).
 */
NTSTATUS ObInsertHandle(HANDLE_TABLE *Table, void *Object, ACCESS_MASK GrantedAccess,
                        ULONG Attributes, HANDLE *Handle);
NTSTATUS ObReferenceObjectByHandle(HANDLE_TABLE *Table, HANDLE Handle,
                                   ACCESS_MASK DesiredAccess, OBJECT_TYPE *Type,
                                   void **Object);
NTSTATUS ObCloseHandle(HANDLE_TABLE *Table, HANDLE Handle);
NTSTATUS ObDuplicateHandle(HANDLE_TABLE *Source, HANDLE SourceHandle, HANDLE_TABLE *Target,
                           ACCESS_MASK DesiredAccess, HANDLE *TargetHandle);

/*
 * Namespace lookup, case-insensitive and case-preserving (ADR-0011). PASSIVE_LEVEL.
 * Maps generic rights in DesiredAccess with the type's GenericMapping, then calls
 * SeAccessCheck with SeGetCurrentToken(). On success the object is referenced for the caller
 * and *GrantedAccess holds the rights actually granted: that is the value to pass to
 * ObInsertHandle when the caller opens a handle.
 * Errors: STATUS_OBJECT_NAME_NOT_FOUND, STATUS_ACCESS_DENIED, STATUS_INVALID_PARAMETER.
 */
NTSTATUS ObReferenceObjectByName(const UNICODE_STRING *Path, OBJECT_TYPE *Type,
                                 ACCESS_MASK DesiredAccess, ACCESS_MASK *GrantedAccess,
                                 void **Object);

#endif /* KAMOS_OB_H */

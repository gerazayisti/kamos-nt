/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * I/O manager and driver model.
 * Owner: A08. Contract state: provisional until the Wave 4 gate.
 * Drivers are linked statically through a registration table until v0.2.
 * Windows driver compatibility is out of scope (ADR-0012).
 */
#ifndef KAMOS_IO_H
#define KAMOS_IO_H

#include <kamos/status.h>
#include <kamos/types.h>

typedef struct _DRIVER_OBJECT DRIVER_OBJECT;   /* Opaque. */
typedef struct _DEVICE_OBJECT DEVICE_OBJECT;   /* Opaque. */
typedef struct _IO_REQUEST IO_REQUEST;         /* Opaque; Kamos-NT design. */

typedef enum _IO_MAJOR_FUNCTION {
    IoMajorCreate = 0,
    IoMajorClose,
    IoMajorRead,
    IoMajorWrite,
    IoMajorDeviceControl,
    IoMajorCount
} IO_MAJOR_FUNCTION;

typedef NTSTATUS (*IO_DISPATCH_ROUTINE)(DEVICE_OBJECT *Device, IO_REQUEST *Request);
typedef NTSTATUS (*DRIVER_ENTRY)(DRIVER_OBJECT *Driver);

typedef struct _KAMOS_BUILTIN_DRIVER {
    const char  *Name;
    DRIVER_ENTRY Entry;
} KAMOS_BUILTIN_DRIVER;

/* PASSIVE_LEVEL. */
NTSTATUS IoSetDispatchRoutine(DRIVER_OBJECT *Driver, IO_MAJOR_FUNCTION Major,
                              IO_DISPATCH_ROUTINE Routine);
NTSTATUS IoCreateDevice(DRIVER_OBJECT *Driver, SIZE_T ExtensionSize,
                        const UNICODE_STRING *Name, DEVICE_OBJECT **Device);
void     IoDeleteDevice(DEVICE_OBJECT *Device);

/*
 * Completes a request exactly once. A second completion is a bug check in debug builds.
 * Up to DISPATCH_LEVEL. An interrupt handler runs at DEVICE_LEVEL, so it queues a DPC
 * (ke.h) and the DPC routine completes the request.
 */
void IoCompleteRequest(IO_REQUEST *Request, NTSTATUS Status, SIZE_T Information);

/* Requests cancellation; the owner completes with STATUS_CANCELLED later. */
BOOLEAN IoCancelRequest(IO_REQUEST *Request);

#endif /* KAMOS_IO_H */

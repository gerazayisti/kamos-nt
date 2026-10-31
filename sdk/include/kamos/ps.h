/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Processes, user threads, system call dispatch (ADR-0006).
 * Owner: A07. Contract state: provisional until the Wave 3 gate.
 * Process and thread structures are Kamos-NT designs and stay opaque here.
 */
#ifndef KAMOS_PS_H
#define KAMOS_PS_H

#include <kamos/status.h>
#include <kamos/types.h>

typedef struct _EPROCESS EPROCESS;   /* Opaque. */
typedef struct _ETHREAD ETHREAD;     /* Opaque. */

/*
 * Creates a process from an image held in memory (boot module until Phase 10).
 * PASSIVE_LEVEL. Errors: STATUS_INVALID_IMAGE_FORMAT, STATUS_NO_MEMORY,
 * STATUS_INSUFFICIENT_RESOURCES.
 */
NTSTATUS PsCreateProcessFromImage(const void *Image, SIZE_T ImageSize,
                                  const UNICODE_STRING *Name, EPROCESS **Process);

EPROCESS *PsGetCurrentProcess(void);
ETHREAD  *PsGetCurrentThread(void);

/* Terminates a process and releases its resources. PASSIVE_LEVEL. */
NTSTATUS PsTerminateProcess(EPROCESS *Process, NTSTATUS ExitStatus);

/* Waits for a process to exit and returns its exit status. PASSIVE_LEVEL. */
NTSTATUS PsWaitForProcess(EPROCESS *Process, NTSTATUS *ExitStatus);

#endif /* KAMOS_PS_H */

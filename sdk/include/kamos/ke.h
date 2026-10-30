/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Kernel core: spinlocks, threads, waits (ADR-0009).
 * Owner: A05. Contract state: provisional until the Wave 2 gate.
 */
#ifndef KAMOS_KE_H
#define KAMOS_KE_H

#include <kamos/irql.h>
#include <kamos/status.h>
#include <kamos/types.h>

/* Ticket spinlock. Layout private to A05; callers only use the functions below. */
typedef struct _KSPIN_LOCK {
    volatile ULONG Next;
    volatile ULONG Owner;
} KSPIN_LOCK;

void KeInitializeSpinLock(KSPIN_LOCK *Lock);
/* Raises to DISPATCH_LEVEL. Caller at or below DISPATCH_LEVEL. */
void KeAcquireSpinLock(KSPIN_LOCK *Lock, KIRQL *OldIrql);
void KeReleaseSpinLock(KSPIN_LOCK *Lock, KIRQL OldIrql);
/* Raises to DEVICE_LEVEL; for data shared with interrupt handlers. */
void KeAcquireInterruptSpinLock(KSPIN_LOCK *Lock, KIRQL *OldIrql);
void KeReleaseInterruptSpinLock(KSPIN_LOCK *Lock, KIRQL OldIrql);

typedef struct _KTHREAD KTHREAD;   /* Opaque. */
typedef void (*KSTART_ROUTINE)(void *Context);

/* Creates a kernel thread in the ready state. PASSIVE_LEVEL. */
NTSTATUS KeCreateThread(KSTART_ROUTINE Start, void *Context, LONG Priority, KTHREAD **Thread);
KTHREAD *KeGetCurrentThread(void);
/* Terminates the calling thread. Its stack is freed by another thread. Never returns. */
_Noreturn void KeExitThread(void);

/* Dispatcher objects: notification and synchronisation events. */
typedef struct _KEVENT KEVENT;     /* Opaque until A05 freezes it. */
typedef enum _EVENT_TYPE { NotificationEvent = 0, SynchronizationEvent = 1 } EVENT_TYPE;

/*
 * Waits on an object. Timeout100ns: NULL waits forever, 0 polls.
 * Returns STATUS_SUCCESS or STATUS_TIMEOUT. PASSIVE_LEVEL (DISPATCH_LEVEL with a zero timeout).
 */
NTSTATUS KeWaitForSingleObject(void *Object, const LONG64 *Timeout100ns);
LONG     KeSetEvent(KEVENT *Event);
void     KeClearEvent(KEVENT *Event);

/* Sleeps the current thread. PASSIVE_LEVEL. */
NTSTATUS KeDelayExecutionThread(LONG64 Interval100ns);

#endif /* KAMOS_KE_H */

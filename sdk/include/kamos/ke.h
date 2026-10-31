/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Kernel core: locks, deferred work, threads, waits (ADR-0009, RFC-0001).
 * Owner: A05. Contract state: provisional until the Wave 2 gate.
 */
#ifndef KAMOS_KE_H
#define KAMOS_KE_H

#include <kamos/irql.h>
#include <kamos/status.h>
#include <kamos/types.h>

/*
 * Lock ranks (docs/architecture/lock-order.md). A lock is taken only while holding locks of
 * strictly lower rank. Debug builds assert this and the execution level at every acquire.
 * LOCK_RANK_NONE exists for host and kernel tests only; production code never uses it.
 */
#define LOCK_RANK_NONE          0u
#define LOCK_RANK_HANDLE_TABLE  10u
#define LOCK_RANK_NAMESPACE     20u
#define LOCK_RANK_ADDRESS_SPACE 30u
#define LOCK_RANK_DISPATCHER    50u
#define LOCK_RANK_POOL          60u
#define LOCK_RANK_FRAME_DB      70u
#define LOCK_RANK_DEBUG_CONSOLE 90u

/* Ticket spinlock. Fields are private to A05; callers only use the functions below. */
typedef struct _KSPIN_LOCK {
    volatile ULONG Next;
    volatile ULONG Owner;
    ULONG          Rank;
    ULONG          Reserved;
} KSPIN_LOCK;

void KeInitializeSpinLock(KSPIN_LOCK *Lock, ULONG Rank);
/* Raises to DISPATCH_LEVEL. Caller at or below DISPATCH_LEVEL. */
void KeAcquireSpinLock(KSPIN_LOCK *Lock, KIRQL *OldIrql);
void KeReleaseSpinLock(KSPIN_LOCK *Lock, KIRQL OldIrql);
/* Raises to DEVICE_LEVEL; for data shared with interrupt handlers. */
void KeAcquireInterruptSpinLock(KSPIN_LOCK *Lock, KIRQL *OldIrql);
void KeReleaseInterruptSpinLock(KSPIN_LOCK *Lock, KIRQL OldIrql);

/*
 * Dispatcher objects. Every waitable object starts with a DISPATCHER_HEADER, which is protected
 * by the dispatcher lock (rank LOCK_RANK_DISPATCHER). Layouts are public only so that objects can
 * be embedded in caller structures without allocating; callers never touch the fields.
 */
typedef enum _DISPATCHER_TYPE {
    DispatcherInvalid = 0,
    DispatcherEvent   = 1,
    DispatcherMutex   = 2,
} DISPATCHER_TYPE;

typedef struct _DISPATCHER_HEADER {
    UCHAR      Type;          /* DISPATCHER_TYPE; checked in debug builds. */
    UCHAR      Signaled;
    USHORT     Reserved;
    LIST_ENTRY WaitList;
} DISPATCHER_HEADER;

typedef enum _EVENT_TYPE { NotificationEvent = 0, SynchronizationEvent = 1 } EVENT_TYPE;

typedef struct _KEVENT {
    DISPATCHER_HEADER Header;
    UCHAR             EventType;   /* EVENT_TYPE. */
} KEVENT;

/* Any level up to DISPATCH_LEVEL. */
void KeInitializeEvent(KEVENT *Event, EVENT_TYPE Type, BOOLEAN InitialState);
LONG KeSetEvent(KEVENT *Event);
void KeClearEvent(KEVENT *Event);

/*
 * Mutex: blocking, non-recursive, owned by one thread, released by that thread. PASSIVE_LEVEL
 * only, and the caller holds no spinlock. This is the lock type of ranks 10 to 30.
 */
typedef struct _KTHREAD KTHREAD;   /* Opaque. */

typedef struct _KMUTEX {
    DISPATCHER_HEADER Header;
    KTHREAD          *Owner;
    ULONG             Rank;
} KMUTEX;

void KeInitializeMutex(KMUTEX *Mutex, ULONG Rank);
void KeAcquireMutex(KMUTEX *Mutex);
void KeReleaseMutex(KMUTEX *Mutex);

/*
 * Waits on an event or mutex (any object that starts with a DISPATCHER_HEADER).
 * Timeout100ns is a relative interval in 100 ns units and never negative (a negative value is
 * STATUS_INVALID_PARAMETER): NULL waits forever, 0 polls. Returns STATUS_SUCCESS or STATUS_TIMEOUT.
 * PASSIVE_LEVEL, or up to DISPATCH_LEVEL when the timeout is 0.
 */
NTSTATUS KeWaitForSingleObject(void *Object, const LONG64 *Timeout100ns);

/*
 * Deferred procedure call: work that a DEVICE_LEVEL handler hands to DISPATCH_LEVEL. The
 * caller owns the KDPC and keeps it alive until the routine has run. No allocation happens
 * when queueing, so it is safe in an interrupt handler.
 */
typedef struct _KDPC KDPC;
typedef void (*KDEFERRED_ROUTINE)(KDPC *Dpc, void *Context);

struct _KDPC {
    LIST_ENTRY        Link;
    KDEFERRED_ROUTINE Routine;
    void             *Context;
    volatile ULONG    Queued;
};

void KeInitializeDpc(KDPC *Dpc, KDEFERRED_ROUTINE Routine, void *Context);
/*
 * Up to DEVICE_LEVEL. Returns false if the DPC is already queued. The routine runs at
 * DISPATCH_LEVEL on the same processor once the level drops below DISPATCH_LEVEL.
 */
BOOLEAN KeInsertQueueDpc(KDPC *Dpc);

/*
 * Work item: work that needs PASSIVE_LEVEL (may block, may allocate) requested from a higher
 * level, for example the deferred deletion of an object. Runs on a system worker thread.
 */
typedef struct _KWORK_ITEM KWORK_ITEM;
typedef void (*KWORK_ROUTINE)(KWORK_ITEM *Item, void *Context);

struct _KWORK_ITEM {
    LIST_ENTRY     Link;
    KWORK_ROUTINE  Routine;
    void          *Context;
    volatile ULONG Queued;
};

void KeInitializeWorkItem(KWORK_ITEM *Item, KWORK_ROUTINE Routine, void *Context);
/* Up to DISPATCH_LEVEL. Returns false if the item is already queued. */
BOOLEAN KeQueueWorkItem(KWORK_ITEM *Item);

typedef void (*KSTART_ROUTINE)(void *Context);

/* Creates a kernel thread in the ready state. PASSIVE_LEVEL. */
NTSTATUS KeCreateThread(KSTART_ROUTINE Start, void *Context, LONG Priority, KTHREAD **Thread);
KTHREAD *KeGetCurrentThread(void);
/* Terminates the calling thread. Its stack is freed by another thread. Never returns. */
_Noreturn void KeExitThread(void);

/* Sleeps the current thread. Interval100ns is relative and not negative. PASSIVE_LEVEL. */
NTSTATUS KeDelayExecutionThread(LONG64 Interval100ns);

#endif /* KAMOS_KE_H */

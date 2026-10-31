/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Software execution levels (ADR-0009 as amended by RFC-0001 and RFC-0002).
 * DEVICE_LEVEL and above run with IF clear; DISPATCH_LEVEL is a pending bit that runs when the
 * level is below DISPATCH_LEVEL, on the current stack, never nested (depth 1). Time-critical:
 * every function here is short and does not call the console, except through ASSERT.
 */

#include "halp.h"

HALP_CPU HalpCpu0;

static void DefaultDispatchHandler(void)
{
}

static HAL_DISPATCH_HANDLER DispatchHandler = DefaultDispatchHandler;

void HalSetDispatchInterruptHandler(HAL_DISPATCH_HANDLER Handler)
{
    DispatchHandler = Handler != NULL ? Handler : DefaultDispatchHandler;
}

KIRQL KeGetCurrentIrql(void)
{
    return HalpCurrentCpu()->Irql;
}

void KeRaiseIrql(KIRQL NewIrql, KIRQL *OldIrql)
{
    HALP_CPU *cpu = HalpCurrentCpu();
    KIRQL     cur = cpu->Irql;

    ASSERT(NewIrql >= cur);
    ASSERT(NewIrql <= HIGH_LEVEL);
    if (NewIrql < cur) {
        NewIrql = cur; /* release build: never lower through Raise */
    }
    if (OldIrql != NULL) {
        *OldIrql = cur;
    }
    if (NewIrql >= DEVICE_LEVEL && cur < DEVICE_LEVEL) {
        __asm__ volatile("cli" : : : "memory");
    }
    cpu->Irql = NewIrql;
}

/*
 * Runs the dispatch software interrupt while it is pending. Preconditions: the level is already
 * below DISPATCH_LEVEL and no software interrupt is running. IF may be set or clear on entry and
 * is as it was on exit. The handler runs with IF set. A request made while it runs only sets the
 * bit again: the loop below picks it up (depth 1).
 */
void HalpDeliverSoftwareInterrupts(void)
{
    HALP_CPU *cpu = HalpCurrentCpu();
    ULONG64   flags;
    KIRQL     saved;

    flags = ArchIrqSave();
    if ((cpu->Pending & HALP_PENDING_DISPATCH) == 0 || cpu->SoftActive != 0 ||
        cpu->Irql >= DISPATCH_LEVEL) {
        ArchIrqRestore(flags);
        return;
    }
    saved           = cpu->Irql;
    cpu->SoftActive = 1;
    cpu->Irql       = DISPATCH_LEVEL;
    cpu->SoftDepth++;
    if (cpu->SoftDepth > cpu->SoftMaxDepth) {
        cpu->SoftMaxDepth = cpu->SoftDepth;
    }
    while ((cpu->Pending & HALP_PENDING_DISPATCH) != 0) {
        cpu->Pending &= ~HALP_PENDING_DISPATCH;
        cpu->SoftEntries++;
        __asm__ volatile("sti" : : : "memory");
        DispatchHandler();
        __asm__ volatile("cli" : : : "memory");
    }
    cpu->SoftDepth--;
    cpu->SoftActive = 0;
    cpu->Irql       = saved;
    ArchIrqRestore(flags);
}

void KeLowerIrql(KIRQL NewIrql)
{
    HALP_CPU *cpu = HalpCurrentCpu();
    KIRQL     cur = cpu->Irql;

    ASSERT(NewIrql <= cur);
    ASSERT(cpu->IntSectionDepth == 0);
    if (NewIrql >= cur) {
        return; /* release build: never raise through Lower */
    }
    if (cur >= DEVICE_LEVEL && NewIrql < DEVICE_LEVEL) {
        /* IF is still clear: finish the bookkeeping, deliver, then open the gate. */
        cpu->Irql = NewIrql;
        HalpDeliverSoftwareInterrupts();
        __asm__ volatile("sti" : : : "memory");
        return;
    }
    cpu->Irql = NewIrql;
    if (NewIrql < DISPATCH_LEVEL) {
        HalpDeliverSoftwareInterrupts();
    }
}

void HalRequestDispatchInterrupt(void)
{
    HALP_CPU *cpu   = HalpCurrentCpu();
    ULONG64   flags = ArchIrqSave();

    cpu->Pending |= HALP_PENDING_DISPATCH;
    ArchIrqRestore(flags);
    HalpDeliverSoftwareInterrupts();
}

BOOLEAN HalDisableInterrupts(void)
{
    HALP_CPU *cpu   = HalpCurrentCpu();
    ULONG64   flags = ArchIrqSave();

    cpu->IntSectionDepth++;
    return (flags & ARCH_RFLAGS_IF) != 0;
}

void HalRestoreInterrupts(BOOLEAN WasEnabled)
{
    HALP_CPU *cpu = HalpCurrentCpu();

    ASSERT(cpu->IntSectionDepth > 0);
    if (cpu->IntSectionDepth > 0) {
        cpu->IntSectionDepth--;
    }
    if (WasEnabled && cpu->IntSectionDepth == 0 && cpu->Irql < DEVICE_LEVEL) {
        __asm__ volatile("sti" : : : "memory");
    }
}

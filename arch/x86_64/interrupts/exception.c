/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Exception dispatch and the page fault policy of ADR-0013 (Intel SDM vol. 3, ch. 6).
 * Handlers run at DEVICE_LEVEL with IF clear. The level of the interrupted code is read at entry
 * (KiTrapDispatch) and put back on exit; ADR-0013 rule 1 uses that level.
 * Line format: docs/architecture/interrupts.md.
 */

#include "../../../hal/halp.h"
#include "fixup.h"

extern const KFIXUP_ENTRY __kfixup_start[];
/* Absolute linker symbol: its ADDRESS is the number of entries (kernel.ld). */
extern const char __kfixup_count[];

KI_EXCEPTION_RECORD KiLastException;
char                KiLastExceptionLine[256];

typedef struct _TEST_SLOT {
    KFIXUP_ENTRY Entry;
    int          Class; /* 0 = free */
} TEST_SLOT;

static TEST_SLOT TestSlots[KI_TEST_FIXUP_SLOTS];

BOOLEAN KiTestFixupRegister(ULONG64 FaultRip, ULONG64 ResumeRip, int Class)
{
    ULONG64 flags = ArchIrqSave();

    for (ULONG i = 0; i < KI_TEST_FIXUP_SLOTS; i++) {
        if (TestSlots[i].Class == 0) {
            TestSlots[i].Entry.FaultRip  = FaultRip;
            TestSlots[i].Entry.ResumeRip = ResumeRip;
            TestSlots[i].Class           = Class;
            ArchIrqRestore(flags);
            return true;
        }
    }
    ArchIrqRestore(flags);
    return false;
}

void KiTestFixupClear(void)
{
    ULONG64 flags = ArchIrqSave();

    for (ULONG i = 0; i < KI_TEST_FIXUP_SLOTS; i++) {
        TestSlots[i].Class = 0;
    }
    ArchIrqRestore(flags);
}

BOOLEAN KiVectorHasErrorCode(ULONG Vector)
{
    return Vector == 8 || (Vector >= 10 && Vector <= 14) || Vector == 17 || Vector == 21 ||
           Vector == 29 || Vector == 30;
}

const char *KiExceptionName(ULONG Vector, char *Scratch, SIZE_T ScratchSize)
{
    static const char *const names[32] = {
        "DE",  "DB",  "NMI", "BP",  "OF",  "BR",  "UD",  "NM",  "DF", "V9",  "TS",
        "NP",  "SS",  "GP",  "PF",  "V15", "MF",  "AC",  "MC",  "XM", "VE",  "CP",
        "V22", "V23", "V24", "V25", "V26", "V27", "V28", "V29", "SX", "V31",
    };

    if (Vector < 32) {
        return names[Vector];
    }
    (void)RtlFormat(Scratch, ScratchSize, "V%u", Vector);
    return Scratch;
}

/* The slot of class Class matching Rip, or NULL. */
static const KFIXUP_ENTRY *FindTestSlot(ULONG64 Rip, int Class)
{
    for (ULONG i = 0; i < KI_TEST_FIXUP_SLOTS; i++) {
        if (TestSlots[i].Class == Class && TestSlots[i].Entry.FaultRip == Rip) {
            return &TestSlots[i].Entry;
        }
    }
    return NULL;
}

void KiExceptionDispatch(KTRAP_FRAME *Tf, KIRQL Interrupted)
{
    ULONG   v      = (ULONG)Tf->Vector;
    BOOLEAN hasErr = KiVectorHasErrorCode(v);
    ULONG64 cr2    = (v == 14) ? ArchReadCr2() : 0; /* first, before anything can fault again */
    const KFIXUP_ENTRY *fix = NULL;
    char                scratch[16];
    char                errText[24];
    char                cr2Text[32];

    /* Policy. NMI, double fault and machine check are never recovered. */
    if (v != 2 && v != 8 && v != 18) {
        fix = FindTestSlot(Tf->Rip, KI_FIXUP_CLASS_EXPECT);           /* any vector, any level */
        if (fix == NULL && v == 14 && Interrupted == PASSIVE_LEVEL) { /* ADR-0013 rule 1 */
            fix = KiFindFixup(__kfixup_start, (size_t)(uintptr_t)__kfixup_count, Tf->Rip);
            if (fix == NULL) {
                fix = FindTestSlot(Tf->Rip, KI_FIXUP_CLASS_FIXUP);
            }
        }
    }

    if (hasErr) {
        (void)RtlFormat(errText, sizeof errText, "0x%lx", (unsigned long)Tf->ErrorCode);
    } else {
        (void)RtlFormat(errText, sizeof errText, "none");
    }
    if (v == 14) {
        (void)RtlFormat(cr2Text, sizeof cr2Text, " cr2=0x%016lx", (unsigned long)cr2);
    } else {
        cr2Text[0] = '\0';
    }

    KiLastException.Count++;
    KiLastException.Vector          = v;
    KiLastException.HasErrorCode    = hasErr;
    KiLastException.Resumed         = (fix != NULL);
    KiLastException.InterruptedIrql = Interrupted;
    KiLastException.HandlerIrql     = KeGetCurrentIrql();
    KiLastException.HandlerIf       = (ArchReadRflags() & ARCH_RFLAGS_IF) != 0;
    KiLastException.ErrorCode       = hasErr ? Tf->ErrorCode : 0;
    KiLastException.Rip             = Tf->Rip;
    KiLastException.Cs              = Tf->Cs;
    KiLastException.Rflags          = Tf->Rflags;
    KiLastException.Rsp             = Tf->Rsp;
    KiLastException.Cr2             = cr2;
    KiLastException.FrameAddress    = (ULONG64)(uintptr_t)Tf;

    (void)RtlFormat(KiLastExceptionLine, sizeof KiLastExceptionLine,
                    "EXC v=%u err=%s rip=0x%016lx cs=0x%lx rflags=0x%lx rsp=0x%016lx%s irql=%u "
                    "name=%s action=%s\n",
                    v, errText, (unsigned long)Tf->Rip, (unsigned long)Tf->Cs,
                    (unsigned long)Tf->Rflags, (unsigned long)Tf->Rsp, cr2Text,
                    (unsigned)Interrupted, KiExceptionName(v, scratch, sizeof scratch),
                    fix != NULL ? "resume" : "stop");
    kprintf("%s", KiLastExceptionLine);
    if (v == 14) {
        kprintf("EXC-PF cr2=0x%016lx present=%u write=%u user=%u rsvd=%u fetch=%u\n",
                (unsigned long)cr2, (unsigned)(Tf->ErrorCode & 1),
                (unsigned)((Tf->ErrorCode >> 1) & 1), (unsigned)((Tf->ErrorCode >> 2) & 1),
                (unsigned)((Tf->ErrorCode >> 3) & 1), (unsigned)((Tf->ErrorCode >> 4) & 1));
    }

    if (fix != NULL) {
        Tf->Rip = fix->ResumeRip; /* RFLAGS (so IF) and the level come back as they were */
        return;
    }
    if (v == 8) {
        KeBugCheckEx(KAMOS_STOP_DOUBLE_FAULT, Tf->Rip, Tf->Rsp, Tf->ErrorCode,
                     (ULONG_PTR)Tf); /* P4: the frame lies on the IST stack */
    }
    KeBugCheckEx(KAMOS_STOP_UNHANDLED_EXCEPTION, v, Tf->Rip, hasErr ? Tf->ErrorCode : 0, cr2);
}

void KiTrapDispatch(KTRAP_FRAME *Tf)
{
    HALP_CPU *cpu         = HalpCurrentCpu();
    KIRQL     interrupted = cpu->Irql;

    cpu->Irql = interrupted < DEVICE_LEVEL ? DEVICE_LEVEL : interrupted;
    if (Tf->Vector < KI_VECTOR_IRQ_BASE) {
        KiExceptionDispatch(Tf, interrupted);
    } else if (Tf->Vector < KI_VECTOR_IRQ_BASE + 16) {
        HalpIrqDispatch(Tf, interrupted);
        cpu->Irql = interrupted;
        /* ADR-0009: at the end of a DEVICE_LEVEL handler that interrupted code running below
         * DISPATCH_LEVEL, run the pending software interrupts (IF stays clear on return here). */
        if (interrupted < DISPATCH_LEVEL) {
            HalpDeliverSoftwareInterrupts();
        }
        return;
    } else {
        KeBugCheckEx(KAMOS_STOP_UNHANDLED_EXCEPTION, Tf->Vector, Tf->Rip, 0, 0);
    }
    cpu->Irql = interrupted;
}

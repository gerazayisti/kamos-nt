/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Phase 2 exception tests (oracles O5, O6, O7 a, R-a). Each fault is raised from a labelled
 * assembly snippet (faults.asm). The expected rip is the address of the label (from the linker),
 * the vector and the presence of an error code come from the Intel SDM exception table, the
 * page-fault error bits from SDM vol. 3, 4.7, and CR2 from the address the test itself used.
 * Scenarios that must end in a bug check are at the end.
 */

#include "ktest_internal.h"

/* faults.asm */
void           KtDoDe(void);
void           KtDeFault(void);
void           KtDeResume(void);
void           KtDoUd(void);
void           KtUdFault(void);
void           KtUdResume(void);
void           KtDoBp(void);
void           KtBpResume(void);
void           KtDoGpNonCanonical(void);
void           KtGpNcFault(void);
void           KtGpNcResume(void);
void           KtDoGpSelector(void);
void           KtGpSelFault(void);
void           KtGpSelResume(void);
void           KtDoRead(ULONG64 Address);
void           KtReadFault(void);
void           KtReadResume(void);
void           KtDoWrite(ULONG64 Address);
void           KtWriteFault(void);
void           KtWriteResume(void);
void           KtDoCall(ULONG64 Target);
void           KtCallRet(void);
void           KtDoStaticFixup(ULONG64 Address);
ULONG          KtDoPfRegs(ULONG64 Address);
void           KtPfRegsFault(void);
void           KtPfRegsResume(void);
_Noreturn void KtDoBadStack(ULONG64 Address);

extern char KiBootStackTop[];

#define UNMAPPED_HIGH 0xFFFF900000000000ull /* no PML4 entry: ADR-0005 pool range, unmapped */

static const char     RoProbe[] = "read-only";
static volatile UCHAR NxProbe[64];

static ULONG64 A(void (*Fn)(void))
{
    return (ULONG64)(uintptr_t)Fn;
}

/* Registers the exact pair as an EXPECT entry (any vector, any level). */
static BOOLEAN Expect(void (*Fault)(void), void (*Resume)(void))
{
    KiTestFixupClear();
    return KiTestFixupRegister(A(Fault), A(Resume), KI_FIXUP_CLASS_EXPECT);
}

/* Checks the record of the last exception and the printed line against independent expectations. */
static void CheckRecord(ULONG CountBefore, ULONG Vector, BOOLEAN HasErr, ULONG64 Err, ULONG64 Rip,
                        ULONG64 Cr2)
{
    char   expect[96];
    size_t n;

    KTX_CHECK_EQ(KiLastException.Count, CountBefore + 1);
    KTX_CHECK_EQ(KiLastException.Vector, Vector);
    KTX_CHECK_EQ(KiLastException.HasErrorCode, HasErr);
    KTX_CHECK_EQ(KiLastException.ErrorCode, Err);
    KTX_CHECK_EQ(KiLastException.Rip, Rip);
    KTX_CHECK_EQ(KiLastException.Cs, KI_SELECTOR_KCODE);
    KTX_CHECK_EQ(KiLastException.Resumed, 1);
    KTX_CHECK_EQ(KiLastException.Cr2, Vector == 14 ? Cr2 : 0);
    /* No IST for these vectors: the frame is on the boot stack (16 KiB). */
    KTX_CHECK(KiLastException.FrameAddress >= (ULONG64)(uintptr_t)KiBootStackTop - 0x4000 &&
              KiLastException.FrameAddress < (ULONG64)(uintptr_t)KiBootStackTop);
    if (HasErr) {
        n = RtlFormat(expect, sizeof expect, "EXC v=%u err=0x%lx rip=0x%016lx cs=0x8 ", Vector,
                      (unsigned long)Err, (unsigned long)Rip);
    } else {
        n = RtlFormat(expect, sizeof expect, "EXC v=%u err=none rip=0x%016lx cs=0x8 ", Vector,
                      (unsigned long)Rip);
    }
    KTX_CHECK(memcmp(KiLastExceptionLine, expect, n) == 0);
    KtNoteHex("rip", Rip);
}

static void ExcDe(void)
{
    ULONG before = KiLastException.Count;

    KTX_REQUIRE(Expect(KtDeFault, KtDeResume));
    KtDoDe();
    KiTestFixupClear();
    CheckRecord(before, 0, false, 0, A(KtDeFault), 0);
}

static void ExcUd(void)
{
    ULONG before = KiLastException.Count;

    KTX_REQUIRE(Expect(KtUdFault, KtUdResume));
    KtDoUd();
    KiTestFixupClear();
    CheckRecord(before, 6, false, 0, A(KtUdFault), 0);
}

static void ExcBp(void)
{
    ULONG before = KiLastException.Count;

    KTX_REQUIRE(Expect(KtBpResume, KtBpResume)); /* a trap: rip is the next instruction */
    KtDoBp();
    KiTestFixupClear();
    CheckRecord(before, 3, false, 0, A(KtBpResume), 0);
}

static void ExcGpNonCanonical(void)
{
    ULONG before = KiLastException.Count;

    KTX_REQUIRE(Expect(KtGpNcFault, KtGpNcResume));
    KtDoGpNonCanonical();
    KiTestFixupClear();
    CheckRecord(before, 13, true, 0, A(KtGpNcFault), 0); /* #GP(0), not #PF (SDM vol. 3, 6.15) */
}

static void ExcGpSelector(void)
{
    ULONG before = KiLastException.Count;

    KTX_REQUIRE(Expect(KtGpSelFault, KtGpSelResume));
    KtDoGpSelector();
    KiTestFixupClear();
    /* Error code: the selector index and TI bit, with EXT and IDT clear: 0x1230 (SDM vol. 3, 6.13).
     */
    CheckRecord(before, 13, true, 0x1230, A(KtGpSelFault), 0);
}

static void PfCase(ULONG64 ErrBits, ULONG64      Cr2, void (*Fault)(void), void (*Resume)(void),
                   void (*Run)(ULONG64), ULONG64 Arg)
{
    ULONG before = KiLastException.Count;

    KTX_REQUIRE(Expect(Fault, Resume));
    Run(Arg);
    KiTestFixupClear();
    CheckRecord(before, 14, true, ErrBits, A(Fault), Cr2);
}

static void PfNotPresentRead(void)
{
    PfCase(0x0, UNMAPPED_HIGH + 0x123, KtReadFault, KtReadResume, KtDoRead, UNMAPPED_HIGH + 0x123);
}

static void PfNotPresentWrite(void)
{
    PfCase(0x2, UNMAPPED_HIGH + 0x40, KtWriteFault, KtWriteResume, KtDoWrite, UNMAPPED_HIGH + 0x40);
}

static void PfNullGuard(void)
{
    PfCase(0x0, 0x8, KtReadFault, KtReadResume, KtDoRead, 0x8); /* kernel read of the null page */
}

static void PfNxFetch(void)
{
    ULONG   before = KiLastException.Count;
    ULONG64 target = (ULONG64)(uintptr_t)&NxProbe[0];

    KiTestFixupClear();
    KTX_REQUIRE(KiTestFixupRegister(target, A(KtCallRet), KI_FIXUP_CLASS_EXPECT));
    KtDoCall(target);
    KiTestFixupClear();
    /* Present, instruction fetch (bit 4): error 0x11; rip and cr2 are both the target. */
    CheckRecord(before, 14, true, 0x11, target, target);
}

static void PfWriteReadOnly(void)
{
    PfCase(0x3, (ULONG64)(uintptr_t)RoProbe, KtWriteFault, KtWriteResume, KtDoWrite,
           (ULONG64)(uintptr_t)RoProbe);
}

/* ---- ADR-0013 -------------------------------------------------------------------------------- */

static void PolicyFixupResumesAndPreservesState(void)
{
    ULONG   before     = KiLastException.Count;
    ULONG64 ifBefore   = ArchReadRflags() & ARCH_RFLAGS_IF;
    KIRQL   irqlBefore = KeGetCurrentIrql();
    ULONG   intact;

    KiTestFixupClear();
    KTX_REQUIRE(KiTestFixupRegister(A(KtPfRegsFault), A(KtPfRegsResume), KI_FIXUP_CLASS_FIXUP));
    intact = KtDoPfRegs(UNMAPPED_HIGH);
    KiTestFixupClear();
    KTX_CHECK_EQ(intact, 1);                         /* callee-saved registers intact */
    KTX_CHECK_EQ(KiLastException.Count, before + 1); /* the counter: one fault */
    KTX_CHECK_EQ(KiLastException.Vector, 14);
    KTX_CHECK_EQ(KiLastException.Resumed, 1);
    KTX_CHECK_EQ(ArchReadRflags() & ARCH_RFLAGS_IF, ifBefore); /* IF as before the fault */
    KTX_CHECK_EQ(KeGetCurrentIrql(), irqlBefore);              /* level as before the fault */
    KTX_CHECK_EQ(KiLastException.InterruptedIrql, PASSIVE_LEVEL);
    KTX_CHECK((KiLastException.Rflags & ARCH_RFLAGS_IF) == ifBefore);
}

static void PolicyStaticTableFixup(void)
{
    ULONG before = KiLastException.Count;

    KiTestFixupClear();
    KtDoStaticFixup(UNMAPPED_HIGH + 0x10); /* the pair is in .kfixup, not registered at run time */
    KTX_CHECK_EQ(KiLastException.Count, before + 1);
    KTX_CHECK_EQ(KiLastException.Resumed, 1);
    KTX_CHECK_EQ(KiLastException.Vector, 14);
}

static void PolicyFixupInDisabledSection(void)
{
    ULONG   before = KiLastException.Count;
    BOOLEAN was;
    ULONG64 ifInside;
    ULONG64 ifAfter;
    KIRQL   levelAfter;

    KiTestFixupClear();
    KTX_REQUIRE(KiTestFixupRegister(A(KtReadFault), A(KtReadResume), KI_FIXUP_CLASS_FIXUP));
    was = HalDisableInterrupts();
    KtDoRead(UNMAPPED_HIGH + 0x8);
    ifInside   = ArchReadRflags() & ARCH_RFLAGS_IF;
    levelAfter = KeGetCurrentIrql();
    ifAfter    = KiLastException.Rflags & ARCH_RFLAGS_IF;
    HalRestoreInterrupts(was);
    KiTestFixupClear();
    KTX_CHECK_EQ(KiLastException.Count, before + 1);
    KTX_CHECK_EQ(KiLastException.Resumed, 1);
    KTX_CHECK_EQ(ifInside, 0); /* IF still clear after the recovery, inside the section */
    KTX_CHECK_EQ(ifAfter, 0);  /* and it was clear in the interrupted frame */
    KTX_CHECK_EQ(levelAfter, PASSIVE_LEVEL);
    KTX_CHECK((ArchReadRflags() & ARCH_RFLAGS_IF) != 0); /* back to normal after the section */
}

static void PolicyExpectAtDeviceLevelKeepsLevel(void)
{
    ULONG   before = KiLastException.Count;
    KIRQL   old;
    ULONG64 ifInside;
    KIRQL   levelAfter;

    KTX_REQUIRE(Expect(KtDeFault, KtDeResume));
    KeRaiseIrql(DEVICE_LEVEL, &old);
    KtDoDe();
    ifInside   = ArchReadRflags() & ARCH_RFLAGS_IF;
    levelAfter = KeGetCurrentIrql();
    KeLowerIrql(old);
    KiTestFixupClear();
    KTX_CHECK_EQ(KiLastException.Count, before + 1);
    KTX_CHECK_EQ(KiLastException.InterruptedIrql, DEVICE_LEVEL);
    KTX_CHECK_EQ(levelAfter, DEVICE_LEVEL);
    KTX_CHECK_EQ(ifInside, 0);
    KTX_CHECK_EQ(KeGetCurrentIrql(), PASSIVE_LEVEL);
}

static void PolicyHandlerLevelIsDeviceAndFrameOnBootStack(void)
{
    /* The handler ran at DEVICE_LEVEL with IF clear: the flags it saw are those of the interrupted
     * code (IF set), so the check is on the frame address and the level seen by the next fault. */
    ULONG before = KiLastException.Count;

    KTX_REQUIRE(Expect(KtUdFault, KtUdResume));
    KtDoUd();
    KiTestFixupClear();
    KTX_CHECK_EQ(KiLastException.Count, before + 1);
    KTX_CHECK_EQ(KiLastException.InterruptedIrql, PASSIVE_LEVEL);
    KTX_CHECK((KiLastException.Rflags & ARCH_RFLAGS_IF) != 0);
    /* R-a, observed inside the handler (the record is filled by the handler itself): */
    KTX_CHECK_EQ(KiLastException.HandlerIrql, DEVICE_LEVEL);
    KTX_CHECK_EQ(KiLastException.HandlerIf, 0);
}

const KTX_CASE KtExcCases[] = {
    {"exc", "de", ExcDe},
    {"exc", "ud", ExcUd},
    {"exc", "bp_trap", ExcBp},
    {"exc", "gp_noncanonical", ExcGpNonCanonical},
    {"exc", "gp_selector", ExcGpSelector},
    {"pf", "not_present_read", PfNotPresentRead},
    {"pf", "not_present_write", PfNotPresentWrite},
    {"pf", "null_guard", PfNullGuard},
    {"pf", "nx_fetch", PfNxFetch},
    {"pf", "write_read_only", PfWriteReadOnly},
    {"pfpolicy", "fixup_resumes_state_intact", PolicyFixupResumesAndPreservesState},
    {"pfpolicy", "static_table_fixup", PolicyStaticTableFixup},
    {"pfpolicy", "fixup_in_disabled_section", PolicyFixupInDisabledSection},
    {"pfpolicy", "expect_at_device_level", PolicyExpectAtDeviceLevelKeepsLevel},
    {"pfpolicy", "handler_frame", PolicyHandlerLevelIsDeviceAndFrameOnBootStack},
};
const size_t KtExcCaseCount = KAMOS_ARRAY_SIZE(KtExcCases);

/* ---- Scenarios: each ends in a bug check (their own boot) ------------------------------------ */

void KtScenarioPfNoFixup(void)
{
    kprintf("scenario pf-nofixup: kernel fault without fixup\n");
    KiTestFixupClear();
    KtDoRead(UNMAPPED_HIGH + 0x18);
}

void KtScenarioPfOffByOne(void)
{
    kprintf("scenario pf-offbyone: fixup registered one byte off\n");
    KiTestFixupClear();
    (void)KiTestFixupRegister(A(KtReadFault) + 1, A(KtReadResume), KI_FIXUP_CLASS_FIXUP);
    KtDoRead(UNMAPPED_HIGH + 0x28);
}

void KtScenarioPfDevice(void)
{
    KIRQL old;

    kprintf("scenario pf-device: exact fixup, but the interrupted code is at DEVICE_LEVEL\n");
    KiTestFixupClear();
    (void)KiTestFixupRegister(A(KtReadFault), A(KtReadResume), KI_FIXUP_CLASS_FIXUP);
    KeRaiseIrql(DEVICE_LEVEL, &old);
    KtDoRead(UNMAPPED_HIGH + 0x38);
}

void KtScenarioFixupNonPf(void)
{
    kprintf("scenario fixup-nonpf: exact fixup registered for a #DE, at PASSIVE_LEVEL\n");
    KiTestFixupClear();
    /* The same pair as an EXPECT entry would resume; as a FIXUP entry it applies to #PF only. */
    (void)KiTestFixupRegister(A(KtDeFault), A(KtDeResume), KI_FIXUP_CLASS_FIXUP);
    KtDoDe();
    KtScenarioFellThrough("fixup-nonpf");
}

void KtScenarioDoubleFault(void)
{
    kprintf("scenario df: first fault with an unusable stack\n");
    KiTestFixupClear();
    KtDoBadStack(UNMAPPED_HIGH + 0x48);
}

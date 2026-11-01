/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * HAL and architecture private header. Owner: A03. Not part of the public contract (sdk/include).
 * Included by relative path because the kernel build has only -Isdk/include.
 */
#ifndef KAMOS_HALP_H
#define KAMOS_HALP_H

#include <kamos/boot.h>
#include <kamos/bugcheck.h>
#include <kamos/hal.h>
#include <kamos/irql.h>
#include <kamos/types.h>

#include "../ntoskrnl/rtl/rtl.h"
#include "halp_bootinfo.h"

/* ---- Raw CPU access (arch/x86_64/cpu) ---------------------------------------------------- */

static inline ULONG64 ArchReadCr0(void)
{
    ULONG64 v;
    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return v;
}

static inline ULONG64 ArchReadCr2(void)
{
    ULONG64 v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

static inline ULONG64 ArchReadCr3(void)
{
    ULONG64 v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void ArchWriteCr3(ULONG64 v)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory");
}

static inline ULONG64 ArchReadCr4(void)
{
    ULONG64 v;
    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}

static inline void ArchWriteCr4(ULONG64 v)
{
    __asm__ volatile("mov %0, %%cr4" : : "r"(v) : "memory");
}

static inline ULONG64 ArchReadMsr(ULONG Msr)
{
    ULONG lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(Msr));
    return ((ULONG64)hi << 32) | lo;
}

static inline void ArchWriteMsr(ULONG Msr, ULONG64 Value)
{
    __asm__ volatile("wrmsr" : : "c"(Msr), "a"((ULONG)Value), "d"((ULONG)(Value >> 32)));
}

static inline ULONG64 ArchReadTsc(void)
{
    ULONG lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((ULONG64)hi << 32) | lo;
}

static inline ULONG64 ArchReadRflags(void)
{
    ULONG64 v;
    __asm__ volatile("pushfq; popq %0" : "=r"(v));
    return v;
}

#define ARCH_RFLAGS_IF (1ull << 9)

/* Raw interrupt flag access, not counted as a HalDisableInterrupts section. */
static inline ULONG64 ArchIrqSave(void)
{
    ULONG64 f = ArchReadRflags();
    __asm__ volatile("cli" : : : "memory");
    return f;
}

static inline void ArchIrqRestore(ULONG64 Flags)
{
    if ((Flags & ARCH_RFLAGS_IF) != 0) {
        __asm__ volatile("sti" : : : "memory");
    }
}

static inline void ArchInvlpg(const void *Address)
{
    __asm__ volatile("invlpg (%0)" : : "r"(Address) : "memory");
}

#define MSR_EFER 0xC0000080u
#define EFER_LME (1ull << 8)
#define EFER_LMA (1ull << 10)
#define EFER_NXE (1ull << 11)
#define CR0_PE   (1ull << 0)
#define CR0_WP   (1ull << 16)
#define CR0_PG   (1ull << 31)
#define CR4_PAE  (1ull << 5)
#define CR4_SMEP (1ull << 20)
#define CR4_SMAP (1ull << 21)

/* Enables SMEP and SMAP when CPUID reports them. Returns the CR4 bits it set. */
ULONG64 ArchInitCpuFeatures(void);

/* ---- Boot (arch/x86_64/boot) ------------------------------------------------------------- */

extern ULONG KiBootMagic;
extern ULONG KiBootMbiPhysical;

/* Removes the identity alias of the image and makes the rest of the low 2 MiB non-executable. */
void ArchFinishBootMappings(void);

_Noreturn void KiStartKernel(ULONG MultibootMagic, ULONG MbiPhysical);

/* ---- Traps (arch/x86_64/interrupts) ---------------------------------------------------------- */

/*
 * Frame built by the stubs in stubs.asm. Push order: the CPU pushes SS, RSP, RFLAGS, CS, RIP (and
 * an error code for vectors 8, 10 to 14, 17, 21, 29, 30; the stub pushes 0 for the others), then
 * the stub pushes the vector, then RAX, RBX, RCX, RDX, RSI, RDI, RBP, R8 to R15. The first field
 * is therefore the last register pushed.
 */
typedef struct _KTRAP_FRAME {
    ULONG64 R15, R14, R13, R12, R11, R10, R9, R8;
    ULONG64 Rbp, Rdi, Rsi, Rdx, Rcx, Rbx, Rax;
    ULONG64 Vector;
    ULONG64 ErrorCode;
    ULONG64 Rip, Cs, Rflags, Rsp, Ss;
} KTRAP_FRAME;
KAMOS_STATIC_ASSERT(sizeof(KTRAP_FRAME) == 22 * 8, "KTRAP_FRAME must match stubs.asm");

#define KI_SELECTOR_KCODE  0x08u
#define KI_SELECTOR_KDATA  0x10u
#define KI_SELECTOR_TSS    0x30u
#define KI_VECTOR_IRQ_BASE 0x20u

#define KI_IST_STACK_SIZE 8192u

extern UCHAR         KiDoubleFaultStack[KI_IST_STACK_SIZE];  /* IST1 */
extern UCHAR         KiNmiStack[KI_IST_STACK_SIZE];          /* IST2 */
extern UCHAR         KiMachineCheckStack[KI_IST_STACK_SIZE]; /* IST3 */
extern const ULONG64 KiIsrTable[256];

void ArchInitGdt(void);
void ArchInitIdt(void);

BOOLEAN     KiVectorHasErrorCode(ULONG Vector);
const char *KiExceptionName(ULONG Vector, char *Scratch, SIZE_T ScratchSize);

/* Common C entry of every stub. */
void KiTrapDispatch(KTRAP_FRAME *Frame);
void KiExceptionDispatch(KTRAP_FRAME *Frame, KIRQL InterruptedIrql);
void HalpIrqDispatch(KTRAP_FRAME *Frame, KIRQL InterruptedIrql);

/* What the last exception looked like, for the tests (the same facts as the EXC line). */
typedef struct _KI_EXCEPTION_RECORD {
    ULONG   Count;
    ULONG   Vector;
    BOOLEAN HasErrorCode;
    BOOLEAN Resumed;
    KIRQL   InterruptedIrql;
    KIRQL HandlerIrql; /* KeGetCurrentIrql() read inside the handler: must be DEVICE_LEVEL (R-a) */
    BOOLEAN HandlerIf; /* RFLAGS.IF read inside the handler: must be clear */
    ULONG64 ErrorCode;
    ULONG64 Rip, Cs, Rflags, Rsp, Cr2;
    ULONG64 FrameAddress; /* address of the trap frame: shows which stack the handler ran on */
} KI_EXCEPTION_RECORD;
extern KI_EXCEPTION_RECORD KiLastException;
extern char                KiLastExceptionLine[256];

/*
 * Fault fixups (ADR-0013). The static table is the linker section .kfixup, filled by the user copy
 * routines (A04) with pairs of 64-bit addresses {fault rip, resume rip}. The match is on the exact
 * address. Test registrations (at most KI_TEST_FIXUP_SLOTS) come in two classes:
 *  FIXUP  behaves like the static table: #PF only, only when the interrupted code ran at
 *         PASSIVE_LEVEL;
 *  EXPECT any vector, any level; only to test the exception decoder.
 */
#define KI_TEST_FIXUP_SLOTS   8
#define KI_FIXUP_CLASS_FIXUP  1
#define KI_FIXUP_CLASS_EXPECT 2
BOOLEAN KiTestFixupRegister(ULONG64 FaultRip, ULONG64 ResumeRip, int Class);
void    KiTestFixupClear(void);

/* ---- Per-CPU state, execution levels (hal/irql.c) -------------------------------------------- */

#define HALP_PENDING_DISPATCH 0x1u

typedef struct _HALP_CPU {
    KIRQL   Irql;            /* software level, ADR-0009 */
    UCHAR   SoftActive;      /* a software interrupt is running (depth 1) */
    ULONG   Pending;         /* HALP_PENDING_* bits */
    ULONG   IntSectionDepth; /* open HalDisableInterrupts sections */
    ULONG64 SoftEntries;     /* times the dispatch handler ran */
    ULONG   SoftMaxDepth;    /* highest nesting seen (must stay 1) */
    ULONG   SoftDepth;
} HALP_CPU;

extern HALP_CPU HalpCpu0;

static inline HALP_CPU *HalpCurrentCpu(void)
{
    return &HalpCpu0; /* one CPU in v0.1; SMP changes this function only */
}

/*
 * Requests the dispatch software interrupt (DPCs, scheduler). Any level. It runs when the level
 * is, or drops, below DISPATCH_LEVEL; never nested (depth 1).
 */
void HalRequestDispatchInterrupt(void);
/* The routine the software interrupt runs, at DISPATCH_LEVEL with IF set. A05 installs it. */
typedef void (*HAL_DISPATCH_HANDLER)(void);
void HalSetDispatchInterruptHandler(HAL_DISPATCH_HANDLER Handler);
/* Delivers pending software interrupts if the level allows (used at the end of an IRQ). */
void HalpDeliverSoftwareInterrupts(void);

/* ---- PIC, PIT, time (hal/pic.c, hal/time.c) ----------------------------------------------------
 */

void   HalpInitPic(void);
USHORT HalpPicReadRegister(BOOLEAN Isr); /* OCW3 read: ISR if true, else IRR; master low byte */
ULONG  HalpPicSpuriousCount(ULONG Line);
/* Test seam: when set, the next IRQ 0 is handled but its EOI is withheld, leaving IRQ 0 in service.
 */
extern volatile BOOLEAN HalpTestHoldTimerEoi;
void           HalpInitTimer(void); /* programs the PIT, calibrates the TSC, returns with IF set */
extern ULONG64 HalpLostTicks;
extern ULONG64 HalpRateUpdates;            /* once-a-second re-measurements applied */
extern ULONG64 HalpTscHzMin, HalpTscHzMax; /* extremes of every measurement */
ULONG64        HalpTickCount(void);
ULONG64        HalpTscHz(void);
ULONG64        HalpTickPeriod100ns(void);
USHORT         HalpPitReadCounter(void);

/* ---- Console and formatted output (hal/console.c) ---------------------------------------- */

int  kprintf(const char *Format, ...) __attribute__((format(printf, 1, 2)));
void HalpVgaBugCheckScreen(const char *Title, const char *Line1, const char *Line2);

/* ---- Boot information (hal/bootinfo.c, hal/bootinfo_init.c) ------------------------------ */

/* Validates and stores the loader's structure; bug checks on failure. */
void HalInitializeBootInformation(ULONG Magic, ULONG MbiPhysical);

/* ---- Bug check and test scenarios ---------------------------------------------------------- */

/* Runs the in-kernel tests selected by QEMU fw_cfg (hal/ktest). Never returns. */
_Noreturn void KtMain(void);

/*
 * QEMU fw_cfg: copies file Name into Buffer (NUL-terminated); returns its length, -1 when the
 * device has no such file, -2 when there is no fw_cfg device.
 */
int HalpFwCfgReadFile(const char *Name, char *Buffer, ULONG BufferSize);

#endif /* KAMOS_HALP_H */

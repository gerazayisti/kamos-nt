/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Phase 2 tests of the PIC, the PIT, the execution levels and time (oracles O4, O8, R-b).
 * The oracle for level masking is hardware state: RFLAGS (pushfq), the 8259 request and
 * in-service registers (OCW3) and the raw PIT latch, never the HAL's own counters alone.
 */

#include "ktest_internal.h"

static ULONG64 FlagIf(void)
{
    return ArchReadRflags() & ARCH_RFLAGS_IF;
}

/* Spins reading the PIT latch until the counter has wrapped Wraps times (bounded). Returns the
 * number of wraps seen. The counter counts down; a wrap is a value larger than the previous one.
 * Bounded in iterations, and not compared with the host clock (BOARD D16). */
static ULONG SpinPitWraps(ULONG Wraps)
{
    USHORT prev = HalpPitReadCounter();
    ULONG  seen = 0;

    for (ULONG i = 0; i < 20000000 && seen < Wraps; i++) {
        USHORT c = HalpPitReadCounter();
        if (c > prev) {
            seen++;
        }
        prev = c;
    }
    return seen;
}

static void IrqlInterruptFlagPerLevel(void)
{
    KIRQL old;
    KIRQL scratch;

    KTX_CHECK_EQ(KeGetCurrentIrql(), PASSIVE_LEVEL);
    KTX_CHECK_EQ(FlagIf(), ARCH_RFLAGS_IF);
    KeRaiseIrql(DISPATCH_LEVEL, &old);
    KTX_CHECK_EQ(old, PASSIVE_LEVEL);
    KTX_CHECK_EQ(KeGetCurrentIrql(), DISPATCH_LEVEL);
    KTX_CHECK_EQ(FlagIf(), ARCH_RFLAGS_IF);
    KeRaiseIrql(DEVICE_LEVEL, &scratch);
    KTX_CHECK_EQ(scratch, DISPATCH_LEVEL);
    KTX_CHECK_EQ(FlagIf(), 0);
    KeRaiseIrql(HIGH_LEVEL, &scratch);
    KTX_CHECK_EQ(scratch, DEVICE_LEVEL);
    KTX_CHECK_EQ(FlagIf(), 0);
    KeLowerIrql(DEVICE_LEVEL);
    KTX_CHECK_EQ(FlagIf(), 0);
    KeLowerIrql(DISPATCH_LEVEL);
    KTX_CHECK_EQ(FlagIf(), ARCH_RFLAGS_IF);
    KeLowerIrql(old);
    KTX_CHECK_EQ(KeGetCurrentIrql(), PASSIVE_LEVEL);
    KTX_CHECK_EQ(FlagIf(), ARCH_RFLAGS_IF);
}

static void IrqlDisabledSection(void)
{
    BOOLEAN was = HalDisableInterrupts();

    KTX_CHECK(was);
    KTX_CHECK_EQ(FlagIf(), 0);
    KTX_CHECK_EQ(KeGetCurrentIrql(), PASSIVE_LEVEL); /* a section raises no level */
    HalRestoreInterrupts(was);
    KTX_CHECK_EQ(FlagIf(), ARCH_RFLAGS_IF);
    {
        KIRQL   old;
        BOOLEAN w2;

        KeRaiseIrql(DEVICE_LEVEL, &old); /* IF already clear: the section reports it */
        w2 = HalDisableInterrupts();
        KTX_CHECK(!w2);
        HalRestoreInterrupts(w2);
        KTX_CHECK_EQ(FlagIf(), 0);
        KeLowerIrql(old);
    }
}

static void IrqlDeviceHoldsTick(void)
{
    ULONG64 ticks0;
    ULONG64 ticks1;
    USHORT  irr;
    ULONG   wraps;
    KIRQL   old;
    ULONG64 later;

    KeRaiseIrql(DEVICE_LEVEL, &old);
    ticks0 = HalpTickCount();
    wraps  = SpinPitWraps(2); /* about 20 ms of PIT time */
    ticks1 = HalpTickCount();
    irr    = HalpPicReadRegister(false);
    KeLowerIrql(old);
    KTX_REQUIRE_EQ(wraps, 2);     /* the spin finished: the PIT runs */
    KTX_CHECK_EQ(ticks1, ticks0); /* no tick was delivered while masked */
    KTX_CHECK((irr & 1u) != 0);   /* but the 8259 holds IRQ0 pending */
    /* Lowering opened the gate: the tick arrives within a bounded number of PIT wraps. */
    (void)SpinPitWraps(3);
    later = HalpTickCount();
    KTX_CHECK(later > ticks0);
    KtNoteU64("held", ticks1 == ticks0);
}

static void IrqlControlTickAtPassive(void)
{
    ULONG64 t0    = HalpTickCount();
    ULONG   wraps = SpinPitWraps(3);

    KTX_REQUIRE_EQ(wraps, 3);
    KTX_CHECK(HalpTickCount() > t0); /* the control: without masking the ticks do arrive */
    KTX_CHECK_EQ(HalpPicReadRegister(true) & 1u, 0); /* and the EOI was sent: ISR bit 0 clear */
}

/* ---- Software dispatch interrupt --------------------------------------------------------------
 */

static volatile ULONG   DispatchRuns;
static volatile ULONG   DispatchSeenLevel;
static volatile ULONG64 DispatchSeenIf;
static volatile ULONG   DispatchRequeue;
static volatile ULONG64 DispatchFrames[8];
static volatile ULONG   DispatchWaitTick;

/*
 * Nesting observed by the handler itself, independent of the HAL's own SoftMaxDepth counter: a
 * handler that is entered while another run of it has not returned raises HandlerActive above 1.
 */
static volatile ULONG HandlerActive;
static volatile ULONG HandlerMaxActive;

static void CountingHandler(void)
{
    ULONG n = DispatchRuns++;

    if (++HandlerActive > HandlerMaxActive) {
        HandlerMaxActive = HandlerActive;
    }

    if (n < 8) {
        DispatchFrames[n] = (ULONG64)(uintptr_t)__builtin_frame_address(0);
    }
    DispatchSeenLevel = KeGetCurrentIrql();
    DispatchSeenIf    = FlagIf();
    if (DispatchWaitTick != 0) {
        /* Wait inside the handler (IF set, level DISPATCH) for one timer tick to arrive. */
        ULONG64 t        = HalpTickCount();
        DispatchWaitTick = 0;
        __asm__ volatile("cli" : : : "memory");
        while (HalpTickCount() == t) {
            __asm__ volatile("sti; hlt; cli" : : : "memory");
        }
        __asm__ volatile("sti" : : : "memory");
    }
    if (DispatchRequeue != 0) {
        DispatchRequeue--;
        HalRequestDispatchInterrupt(); /* sets the bit; must not nest */
    }
    HandlerActive--;
}

static void DispatchWaitsForLowering(void)
{
    KIRQL old;
    ULONG before;

    /*
     * The timer tick also requests the dispatch interrupt, and at PASSIVE_LEVEL that request runs
     * the installed handler at once. So the level is raised BEFORE the handler is installed and the
     * counter sampled: from then on nothing can run it until the lowering below (this test was
     * flaky when it sampled first: a tick between the sample and the raise added a run).
     */
    KeRaiseIrql(DISPATCH_LEVEL, &old);
    HalSetDispatchInterruptHandler(CountingHandler);
    DispatchRequeue  = 0;
    DispatchWaitTick = 0;
    before           = DispatchRuns;
    HalRequestDispatchInterrupt();
    KTX_CHECK_EQ(DispatchRuns, before); /* held while the level is DISPATCH_LEVEL */
    KeLowerIrql(old);
    /*
     * Delivered when the level dropped below it. "At least once": the handler runs with IF set, a
     * timer tick can arrive inside it and request the dispatch interrupt again, and that makes the
     * delivery loop run it a second time (legitimate; seen once in 3 runs of the exact check).
     */
    KTX_CHECK(DispatchRuns >= before + 1);
    KTX_CHECK_EQ(DispatchSeenLevel, DISPATCH_LEVEL);
    KTX_CHECK_EQ(DispatchSeenIf, ARCH_RFLAGS_IF);
    KTX_CHECK_EQ(KeGetCurrentIrql(), PASSIVE_LEVEL);
    HalSetDispatchInterruptHandler(NULL);
}

static void DispatchImmediateAtPassive(void)
{
    ULONG before;

    HalSetDispatchInterruptHandler(CountingHandler);
    DispatchRequeue  = 0;
    DispatchWaitTick = 0;
    before           = DispatchRuns;
    HalRequestDispatchInterrupt();
    /* PASSIVE_LEVEL: runs at once. A timer tick may add a run of its own, so "at least one". */
    KTX_CHECK(DispatchRuns >= before + 1);
    KTX_CHECK_EQ(KeGetCurrentIrql(), PASSIVE_LEVEL);
    HalSetDispatchInterruptHandler(NULL);
}

static void DispatchHeldAtDeviceLevel(void)
{
    KIRQL old;
    ULONG before;

    KeRaiseIrql(DEVICE_LEVEL, &old); /* before installing and sampling: see the test above */
    HalSetDispatchInterruptHandler(CountingHandler);
    DispatchRequeue  = 0;
    DispatchWaitTick = 0;
    before           = DispatchRuns;
    HalRequestDispatchInterrupt();
    KTX_CHECK_EQ(DispatchRuns, before);
    KeLowerIrql(DISPATCH_LEVEL); /* still not below DISPATCH_LEVEL */
    KTX_CHECK_EQ(DispatchRuns, before);
    KeLowerIrql(PASSIVE_LEVEL);
    KTX_CHECK(DispatchRuns >= before + 1); /* at least once, see the test above */
    KTX_CHECK_EQ(FlagIf(), ARCH_RFLAGS_IF);
    (void)old;
    HalSetDispatchInterruptHandler(NULL);
}

static void DispatchDepthIsOne(void)
{
    HALP_CPU *cpu = HalpCurrentCpu();
    ULONG     before;
    ULONG64   ticks0;

    HalSetDispatchInterruptHandler(CountingHandler);
    cpu->SoftMaxDepth = 0;
    HandlerMaxActive  = 0;
    DispatchRequeue   = 2; /* the handler requeues itself twice ... */
    DispatchWaitTick =
        1; /* ... and a timer tick arrives during the first run (the tick also requests) */
    before = DispatchRuns;
    ticks0 = HalpTickCount();
    HalRequestDispatchInterrupt();
    KTX_CHECK(DispatchRuns >= before + 3); /* the original plus two requeues, looped not nested */
    KTX_CHECK(HalpTickCount() > ticks0);   /* the tick really came during the handler */
    KTX_CHECK_EQ(cpu->SoftMaxDepth, 1);    /* never re-entered (the HAL's own counter) */
    KTX_CHECK_EQ(HandlerMaxActive, 1);     /* and as seen by the handler: never two live at once */
    for (ULONG i = before; i + 1 < DispatchRuns && i + 1 < 8; i++) {
        /*
         * Every run is on the current stack, a short distance below this frame. (Not "the same
         * position each time": a run delivered at the end of the timer IRQ sits below the IRQ
         * frame, a run delivered by the loop does not.)
         */
        ULONG64 here = (ULONG64)(uintptr_t)&before;
        KTX_CHECK(DispatchFrames[i] < here && here - DispatchFrames[i] < 2048);
    }
    KTX_CHECK_EQ(KeGetCurrentIrql(), PASSIVE_LEVEL);
    KtNoteU64("runs", DispatchRuns - before);
    KtNoteU64("handler_max_active", HandlerMaxActive);
    HalSetDispatchInterruptHandler(NULL);
}

static void DispatchFromTimerTick(void)
{
    ULONG   before;
    ULONG64 t0 = HalpTickCount();

    HalSetDispatchInterruptHandler(CountingHandler);
    DispatchRequeue  = 0;
    DispatchWaitTick = 0;
    before           = DispatchRuns;
    (void)SpinPitWraps(4); /* about 40 ms at PASSIVE_LEVEL */
    KTX_CHECK(HalpTickCount() > t0);
    KTX_CHECK(DispatchRuns >
              before); /* the timer handler requested it, the end of the IRQ ran it */
    KTX_CHECK_EQ(DispatchSeenLevel, DISPATCH_LEVEL);
    KTX_CHECK_EQ(KeGetCurrentIrql(), PASSIVE_LEVEL);
    HalSetDispatchInterruptHandler(NULL);
}

/* ---- PIC --------------------------------------------------------------------------------------
 */

static void PicSpurious7(void)
{
    ULONG  c0   = HalpPicSpuriousCount(7);
    USHORT isr0 = HalpPicReadRegister(true);

    __asm__ volatile("int $0x27"); /* a software interrupt does not set the ISR bit: spurious */
    KTX_CHECK_EQ(HalpPicSpuriousCount(7), c0 + 1);
    KTX_CHECK_EQ(HalpPicReadRegister(true), isr0); /* no EOI wrongly clearing another line */
    KTX_CHECK_EQ(KeGetCurrentIrql(), PASSIVE_LEVEL);
    KTX_CHECK_EQ(FlagIf(), ARCH_RFLAGS_IF);
}

/*
 * M16: with IRQ 0 in service, a spurious IRQ 7 must not get an EOI. A non-specific EOI would clear
 * the highest ISR bit, which is IRQ 0's. The seam withholds the EOI of the next timer tick so the
 * ISR bit is set when the spurious interrupt arrives; the test sends that EOI itself afterwards.
 */
static void PicSpuriousKeepsLineInService(void)
{
    ULONG   c0 = HalpPicSpuriousCount(7);
    ULONG64 t0 = HalpTickCount();
    USHORT  isrHeld;
    USHORT  isrAfter;

    HalpTestHoldTimerEoi = true;
    __asm__ volatile("cli" : : : "memory");
    while (HalpTickCount() == t0) {
        __asm__ volatile("sti; hlt; cli" : : : "memory");
    }
    isrHeld = HalpPicReadRegister(true); /* IF is clear here: no tick can intervene */
    __asm__ volatile("int $0x27");
    isrAfter = HalpPicReadRegister(true);
    HalWritePortUchar(0x20, 0x20); /* the withheld EOI */
    __asm__ volatile("sti" : : : "memory");
    KTX_REQUIRE((isrHeld & 1) != 0); /* IRQ 0 really was in service */
    KTX_CHECK_EQ(HalpPicSpuriousCount(7), c0 + 1);
    KTX_CHECK_EQ(isrAfter, isrHeld); /* the spurious interrupt did not clear IRQ 0 */
    KtNoteHex("isr", isrHeld);
}

static void PicSpurious15(void)
{
    ULONG  c0   = HalpPicSpuriousCount(15);
    USHORT isr0 = HalpPicReadRegister(true);

    __asm__ volatile("int $0x2F");
    KTX_CHECK_EQ(HalpPicSpuriousCount(15), c0 + 1);
    KTX_CHECK_EQ(HalpPicReadRegister(true), isr0);
    KTX_CHECK_EQ(KeGetCurrentIrql(), PASSIVE_LEVEL);
}

static BOOLEAN DummyIsr(void *Context)
{
    (void)Context;
    return true;
}

static void PicConnectErrors(void)
{
    KTX_CHECK_EQ(HalConnectInterrupt(16, DummyIsr, NULL), STATUS_INVALID_PARAMETER);
    KTX_CHECK_EQ(HalConnectInterrupt(2, DummyIsr, NULL), STATUS_INVALID_PARAMETER); /* cascade */
    KTX_CHECK_EQ(HalConnectInterrupt(0, DummyIsr, NULL), STATUS_OBJECT_NAME_COLLISION); /* timer */
    KTX_CHECK_EQ(HalConnectInterrupt(5, NULL, NULL), STATUS_INVALID_PARAMETER);
}

/* ---- Time -------------------------------------------------------------------------------------
 */

static ULONG64 TimerStart100ns;
static ULONG64 TimerStartTicks;

static void TimerCalibration(void)
{
    ULONG64 hz = HalpTscHz();

    KTX_CHECK(hz > 100000000ull && hz < 20000000000ull);
    KtNoteU64("tsc_hz", hz);
    KtNoteU64("tick100ns", HalpTickPeriod100ns());
}

static void TimerMonotonic(void)
{
    ULONG64 a = HalQueryMonotonicTime();
    ULONG64 b;

    (void)SpinPitWraps(1);
    b = HalQueryMonotonicTime();
    KTX_CHECK(b > a);
}

/* O4: waits in HLT, never in a polling loop. The host measures the interval between the lines. */
static void TimerStart(void)
{
    TimerStart100ns = HalQueryMonotonicTime();
    TimerStartTicks = HalpTickCount();
    KtNoteU64("t100ns", TimerStart100ns);
}

static void TimerEnd(void)
{
    const ULONG64 span = 10ull * 10000000ull; /* 10 s in 100 ns units */
    ULONG64       now;
    ULONG64       tickTime;
    ULONG64       diff;

    __asm__ volatile("cli" : : : "memory");
    while (HalQueryMonotonicTime() < TimerStart100ns + span) {
        __asm__ volatile("sti; hlt; cli" : : : "memory");
    }
    __asm__ volatile("sti" : : : "memory");
    now      = HalQueryMonotonicTime();
    tickTime = (HalpTickCount() - TimerStartTicks) * HalpTickPeriod100ns();
    diff     = tickTime > now - TimerStart100ns ? tickTime - (now - TimerStart100ns)
                                                : (now - TimerStart100ns) - tickTime;
    kprintf("timer: tsc_hz min %lu max %lu, %lu rate update(s) since boot\n",
            (unsigned long)HalpTscHzMin, (unsigned long)HalpTscHzMax,
            (unsigned long)HalpRateUpdates);
    KtNoteU64("t100ns", now);
    KtNoteU64("tsc_span", now - TimerStart100ns);
    KtNoteU64("tick_span", tickTime);
    /*
     * Cross-check inside the guest: the clock built from the TSC against the tick counter. Ticks
     * can be LOST (the host stalls QEMU, observed on a loaded host: calibration was exact and 10 s
     * of ticks fell more than 1 % short), but they cannot arrive too fast. So only "ticks ahead of
     * the TSC clock by more than 1 %" fails here (a calibration that is too high); a lag is
     * reported in tick_lag_permille and the host wall-clock ratio (O4) is the oracle for the whole
     * path.
     */
    KtNoteU64("tick_lag_permille",
              tickTime < now - TimerStart100ns ? (diff * 1000) / (now - TimerStart100ns) : 0);
    KTX_CHECK(tickTime <= (now - TimerStart100ns) + (now - TimerStart100ns) / 100);
}

const KTX_CASE KtIrqlCases[] = {
    {"pic", "spurious7", PicSpurious7},
    {"pic", "spurious7_irq0_in_service", PicSpuriousKeepsLineInService},
    {"pic", "spurious15", PicSpurious15},
    {"pic", "connect_errors", PicConnectErrors},
    {"irql", "if_per_level", IrqlInterruptFlagPerLevel},
    {"irql", "disabled_section", IrqlDisabledSection},
    {"irql", "control_tick_at_passive", IrqlControlTickAtPassive},
    {"irql", "device_holds_tick", IrqlDeviceHoldsTick},
    {"dispatch", "held_until_lowered", DispatchWaitsForLowering},
    {"dispatch", "immediate_at_passive", DispatchImmediateAtPassive},
    {"dispatch", "held_at_device", DispatchHeldAtDeviceLevel},
    {"dispatch", "depth_is_one", DispatchDepthIsOne},
    {"dispatch", "from_timer_tick", DispatchFromTimerTick},
    {"timer", "calibration", TimerCalibration},
    {"timer", "monotonic", TimerMonotonic},
    {"timer", "start", TimerStart},
    {"timer", "end", TimerEnd},
};
const size_t KtIrqlCaseCount = KAMOS_ARRAY_SIZE(KtIrqlCases);

/* ---- Scenarios (bug check in debug builds; ASSERT is compiled out in release) -----------------
 */

void KtScenarioRaiseDown(void)
{
    KIRQL old;

    kprintf("scenario irql-raise-down: KeRaiseIrql to a lower level\n");
    KeRaiseIrql(DISPATCH_LEVEL, &old);
    KeRaiseIrql(PASSIVE_LEVEL, &old);
    KtScenarioFellThrough("irql-raise-down");
}

void KtScenarioLowerUp(void)
{
    kprintf("scenario irql-lower-up: KeLowerIrql to a higher level\n");
    KeLowerIrql(DISPATCH_LEVEL);
    KtScenarioFellThrough("irql-lower-up");
}

void KtScenarioLowerInSection(void)
{
    BOOLEAN was;

    kprintf("scenario irql-lower-section: KeLowerIrql inside a HalDisableInterrupts section\n");
    was = HalDisableInterrupts();
    KeLowerIrql(PASSIVE_LEVEL);
    HalRestoreInterrupts(was);
    KtScenarioFellThrough("irql-lower-section");
}

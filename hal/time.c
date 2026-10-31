/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * PIT channel 0, TSC calibration and the monotonic clock (RFC-0002 item 8, BOARD D16).
 *
 * HalQueryMonotonicTime comes from the TSC, not from counting ticks, so a lost tick cannot slow it.
 * The TSC frequency is measured against the PIT with INTERRUPT-DRIVEN ticks: the timer handler
 * reads the TSC as its first action, and the boot code waits in HLT. It never polls the PIT
 * counter, because under QEMU TCG a polling guest sees PIT time pass at about 0.8 of host time
 * while a guest that waits in HLT follows within 1 % (docs/testing/oracles-wave1.md section 5).
 *
 * The rate is then RE-MEASURED once a second from the same ticks and the clock is re-based at the
 * moment of the change (continuous, never backwards). Reason, observed on the development host
 * (WSL2, QEMU TCG): the TSC seen by the guest ran at about 2.706 GHz in some boots and at about
 * 2.959 GHz in others, and a few boots changed rate between the one second of calibration and the
 * ten seconds of the timer test (calibration 1 % to 9 % off, ratio against the host clock 1.055,
 * 1.072). A fixed calibration cannot be right on such a host; real hardware with an invariant TSC
 * never changes the rate and the re-measurement then returns the same value.
 * Sources: Intel 8254 data sheet (control word, mode 2); Intel SDM vol. 3, 17.17 (TSC).
 */

#include "halp.h"

/* The PIT input clock is 14.31818 MHz / 12, written as the exact ratio 14318180 / 12 Hz. */
#define PIT_BASE_NUM     14318180ull
#define PIT_BASE_DEN     12ull
#define HALP_PIT_DIVISOR 11932u /* about 100 Hz */
#define CAL_FIRST_TICK   2u
#define CAL_LAST_TICK    102u /* 100 intervals, about 1 s */
#define CAL_SPAN         10u
#define CAL_INTERVALS    (CAL_LAST_TICK - CAL_FIRST_TICK)
#define CAL_WINDOWS      (CAL_INTERVALS - CAL_SPAN + 1) /* 91 */
#define RING_SIZE        128u                           /* power of two, above CAL_LAST_TICK + 1 */
#define RATE_PERIOD      100u                           /* ticks between two re-measurements */

static volatile ULONG64 Ticks;
static ULONG64          TickRing[RING_SIZE]; /* TSC read by the handler, indexed by tick number */

/* The clock: time = Base100ns + ((tsc - BaseTsc) * Mult >> 32). Written only by the timer handler
 * (after the first calibration), under the sequence counter. */
static volatile ULONG   ClockSeq;
static volatile ULONG64 BaseTsc;
static volatile ULONG64 Base100ns;
static volatile ULONG64 Mult; /* (10^7 << 32) / TscHz, 0 until calibrated */
static ULONG64          TscHz;
static volatile BOOLEAN Calibrated;

ULONG64 HalpLostTicks;   /* windows more than 2 % off the median at the first calibration */
ULONG64 HalpRateUpdates; /* re-measurements applied after the first calibration */
ULONG64 HalpTscHzMin, HalpTscHzMax; /* extremes of every measurement, first included */

/*
 * Median, over every window of CAL_SPAN consecutive ticks ending at tick Newest (CAL_WINDOWS
 * windows), of the TSC cost of a window. Delivery jitter under TCG is one-sided (a tick arrives
 * late and the next early, so the sum over a span barely moves) and a lost tick makes a window
 * 1/CAL_SPAN too long; both only move some windows and the median ignores them. Returns TSC cycles
 * per tick with 4 fractional bits; *Outliers counts the windows more than 2 % off the median.
 */
static ULONG64 CyclesPerTick16(ULONG64 Newest, ULONG64 *Outliers)
{
    ULONG64 spans[CAL_WINDOWS];
    ULONG64 median;
    ULONG64 outliers = 0;

    for (ULONG64 i = 0; i < CAL_WINDOWS; i++) {
        ULONG64 d =
            TickRing[(Newest - i) % RING_SIZE] - TickRing[(Newest - i - CAL_SPAN) % RING_SIZE];
        ULONG64 j = i;
        while (j > 0 && spans[j - 1] > d) {
            spans[j] = spans[j - 1];
            j--;
        }
        spans[j] = d;
    }
    median = spans[CAL_WINDOWS / 2];
    for (ULONG64 i = 0; i < CAL_WINDOWS; i++) {
        ULONG64 dev = spans[i] > median ? spans[i] - median : median - spans[i];
        if (dev * 50 > median) {
            outliers++;
        }
    }
    if (Outliers != NULL) {
        *Outliers = outliers;
    }
    return median * 16 / CAL_SPAN;
}

/* hz = cycles per tick / (divisor * 12 / 14318180 s per tick) */
static ULONG64 HzFromCycles16(ULONG64 Cycles16)
{
    return (Cycles16 * PIT_BASE_NUM) / (16 * HALP_PIT_DIVISOR * PIT_BASE_DEN);
}

static ULONG64 ReadClock100ns(ULONG64 Tsc)
{
    return Base100ns + (ULONG64)(((unsigned __int128)(Tsc - BaseTsc) * Mult) >> 32);
}

/* Handler context only (IF clear), after the first calibration. */
static void RemeasureRate(ULONG64 Newest, ULONG64 Tsc)
{
    ULONG64 hz = HzFromCycles16(CyclesPerTick16(Newest, NULL));

    if (hz < TscHz / 2 || hz > TscHz * 2) {
        return; /* not a plausible rate: keep the current one */
    }
    if (hz < HalpTscHzMin) {
        HalpTscHzMin = hz;
    }
    if (hz > HalpTscHzMax) {
        HalpTscHzMax = hz;
    }
    ClockSeq++; /* odd: readers retry */
    __asm__ volatile("" : : : "memory");
    Base100ns = ReadClock100ns(Tsc);
    BaseTsc   = Tsc;
    TscHz     = hz;
    Mult      = (10000000ull << 32) / hz;
    __asm__ volatile("" : : : "memory");
    ClockSeq++;
    HalpRateUpdates++;
}

static BOOLEAN TimerInterrupt(void *Context)
{
    ULONG64 tsc = ArchReadTsc(); /* first: the least latency between the pulse and the sample */
    ULONG64 n;

    (void)Context;
    n                       = Ticks + 1;
    TickRing[n % RING_SIZE] = tsc;
    Ticks                   = n;
    if (Calibrated && n % RATE_PERIOD == 0) {
        RemeasureRate(n, tsc);
    }
    HalRequestDispatchInterrupt(); /* ADR-0009: the timer requests the dispatch interrupt only */
    return true;
}

ULONG64 HalpTickCount(void)
{
    return Ticks;
}

ULONG64 HalpTscHz(void)
{
    return TscHz;
}

/* Time one PIT tick lasts, in 100 ns units (rounded to nearest). */
ULONG64 HalpTickPeriod100ns(void)
{
    ULONG64 num = (ULONG64)HALP_PIT_DIVISOR * PIT_BASE_DEN * 10000000ull;
    return (num + PIT_BASE_NUM / 2) / PIT_BASE_NUM;
}

/* Raw read of the PIT channel 0 down-counter (latch command), for the level-masking tests. */
USHORT HalpPitReadCounter(void)
{
    UCHAR lo, hi;

    HalWritePortUchar(0x43, 0x00); /* latch channel 0 */
    lo = HalReadPortUchar(0x40);
    hi = HalReadPortUchar(0x40);
    return (USHORT)(lo | ((USHORT)hi << 8));
}

ULONG64 HalQueryMonotonicTime(void)
{
    for (;;) {
        ULONG   seq = ClockSeq;
        ULONG64 t;

        if ((seq & 1) == 0 && Calibrated) {
            __asm__ volatile("" : : : "memory");
            t = ReadClock100ns(ArchReadTsc());
            __asm__ volatile("" : : : "memory");
            if (ClockSeq == seq) {
                return t;
            }
        } else if (!Calibrated) {
            return 0;
        }
        /* the handler changed the rate while we read: read again (it runs at most once a second) */
    }
}

/* Waits in HLT, interrupts enabled around it, until the tick counter reaches Target. */
static void WaitTicks(ULONG64 Target)
{
    __asm__ volatile("cli" : : : "memory");
    while (Ticks < Target) {
        __asm__ volatile("sti; hlt; cli" : : : "memory"); /* STI delays recognition by one insn */
    }
    __asm__ volatile("sti" : : : "memory");
}

void HalpInitTimer(void)
{
    ULONG64  hz;
    NTSTATUS st;

    st = HalConnectInterrupt(0, TimerInterrupt, NULL);
    ASSERT(NT_SUCCESS(st));
    (void)st;

    HalWritePortUchar(0x43, 0x34); /* channel 0, lobyte/hibyte, mode 2 (rate generator), binary */
    HalWritePortUchar(0x40, (UCHAR)(HALP_PIT_DIVISOR & 0xFF));
    HalWritePortUchar(0x40, (UCHAR)(HALP_PIT_DIVISOR >> 8));

    WaitTicks(CAL_LAST_TICK);

    hz = HzFromCycles16(CyclesPerTick16(CAL_LAST_TICK, &HalpLostTicks));
    ASSERT(hz != 0);
    if (hz == 0) {
        KeBugCheckEx(KAMOS_STOP_ASSERTION_FAILED, 0x7C, 0, 0, 0); /* no usable TSC */
    }
    __asm__ volatile("cli" : : : "memory");
    TscHz        = hz;
    HalpTscHzMin = HalpTscHzMax = hz;
    Mult                        = (10000000ull << 32) / hz;
    BaseTsc    = TickRing[1]; /* the clock starts at the first tick; earlier time is not counted */
    Base100ns  = 0;
    Calibrated = true;
    __asm__ volatile("sti" : : : "memory");
}

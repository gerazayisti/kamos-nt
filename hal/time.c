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

static volatile ULONG64 Ticks;
static volatile ULONG64 TickTsc[CAL_LAST_TICK + 1];
static ULONG64          Tsc0;  /* TSC when the monotonic clock started */
static ULONG64          TscHz; /* measured */
static ULONG64          Mult;  /* (10^7 << 32) / TscHz, 0 until calibrated */
static BOOLEAN          Calibrated;
ULONG64 HalpLostTicks; /* ticks that calibration inferred were lost (information only) */

static BOOLEAN TimerInterrupt(void *Context)
{
    ULONG64 tsc = ArchReadTsc(); /* first: the least latency between the pulse and the sample */
    ULONG64 n;

    (void)Context;
    n = ++Ticks;
    if (n <= CAL_LAST_TICK) {
        TickTsc[n] = tsc;
    }
    HalRequestDispatchInterrupt(); /* ADR-0009: the timer requests the dispatch interrupt only */
    return true;
}

/* Calibration sample i (tick number i, 1 to CAL_LAST_TICK): the TSC read by the handler. */
ULONG64 HalpCalibrationSample(ULONG I)
{
    return I <= CAL_LAST_TICK ? TickTsc[I] : 0;
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
    ULONG64           delta;
    unsigned __int128 p;

    if (!Calibrated) {
        return 0;
    }
    delta = ArchReadTsc() - Tsc0;
    p     = (unsigned __int128)delta * Mult;
    return (ULONG64)(p >> 32);
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
    ULONG64  dTsc;
    ULONG64  dTicks = CAL_LAST_TICK - CAL_FIRST_TICK;
    NTSTATUS st;

    st = HalConnectInterrupt(0, TimerInterrupt, NULL);
    ASSERT(NT_SUCCESS(st));
    (void)st;

    Tsc0 = ArchReadTsc();
    HalWritePortUchar(0x43, 0x34); /* channel 0, lobyte/hibyte, mode 2 (rate generator), binary */
    HalWritePortUchar(0x40, (UCHAR)(HALP_PIT_DIVISOR & 0xFF));
    HalWritePortUchar(0x40, (UCHAR)(HALP_PIT_DIVISOR >> 8));

    WaitTicks(CAL_LAST_TICK);

    /*
     * Least-squares slope of TSC against tick number over all samples, so that the delivery
     * jitter of single ticks (about a millisecond under TCG) averages out instead of landing in
     * two end points. x = tick - CAL_FIRST_TICK (0..N-1), y = TSC - first TSC (fits 64 bits:
     * N * sum(x*y) is below 2^52 for a 1 s window at 4 GHz).
     *   slope = (N*sum(xy) - sum(x)*sum(y)) / (N*sum(xx) - sum(x)^2)    [TSC cycles per tick]
     */
    {
        /*
         * Median of the TSC cost of CAL_SPAN consecutive ticks, over every window. Delivery
         * jitter under TCG is one-sided (a tick arrives late, the next one early, so the sum over a
         * span barely moves) and a lost tick makes a span 1/CAL_SPAN too long; both only move a few
         * windows out of 90, and the median ignores them. HalpLostTicks counts the windows more
         * than 2 % off the median (information only).
         */
        ULONG64 spans[CAL_LAST_TICK];
        ULONG64 count = dTicks - CAL_SPAN + 1;
        ULONG64 median;

        for (ULONG64 i = 0; i < count; i++) {
            ULONG64 d = TickTsc[CAL_FIRST_TICK + i + CAL_SPAN] - TickTsc[CAL_FIRST_TICK + i];
            ULONG64 j = i;
            while (j > 0 && spans[j - 1] > d) {
                spans[j] = spans[j - 1];
                j--;
            }
            spans[j] = d;
        }
        median        = spans[count / 2];
        HalpLostTicks = 0;
        for (ULONG64 i = 0; i < count; i++) {
            ULONG64 dev = spans[i] > median ? spans[i] - median : median - spans[i];
            if (dev * 50 > median) {
                HalpLostTicks++;
            }
        }
        dTsc = median * 16 / CAL_SPAN; /* cycles per tick, 4 fractional bits */
    }
    /* hz = cycles per tick / (divisor * 12 / 14318180 s per tick) */
    TscHz = (dTsc * PIT_BASE_NUM) / (16 * HALP_PIT_DIVISOR * PIT_BASE_DEN);
    ASSERT(TscHz != 0);
    if (TscHz == 0) {
        KeBugCheckEx(KAMOS_STOP_ASSERTION_FAILED, 0x7C, 0, 0, 0); /* no usable TSC */
    }
    Mult       = (10000000ull << 32) / TscHz;
    Tsc0       = TickTsc[1]; /* the clock starts at the first tick; earlier time is not counted */
    Calibrated = true;
}

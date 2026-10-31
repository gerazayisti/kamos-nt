/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Fatal stop (ADR-0007, bugcheck.h): serial line, VGA screen, exit through isa-debug-exit. */

#include "halp.h"

static const char *StopName(KAMOS_STOP_CODE Code)
{
    switch (Code) {
    case KAMOS_STOP_ASSERTION_FAILED:
        return "ASSERTION_FAILED";
    case KAMOS_STOP_UNHANDLED_EXCEPTION:
        return "UNHANDLED_EXCEPTION";
    case KAMOS_STOP_DOUBLE_FAULT:
        return "DOUBLE_FAULT";
    case KAMOS_STOP_IRQL_VIOLATION:
        return "IRQL_VIOLATION";
    case KAMOS_STOP_POOL_CORRUPTION:
        return "POOL_CORRUPTION";
    case KAMOS_STOP_BAD_BOOT_INFORMATION:
        return "BAD_BOOT_INFORMATION";
    default:
        return "UNKNOWN";
    }
}

void DbgPrintAssertFailure(const char *Expr, const char *File, int Line)
{
    kprintf("ASSERT FAILED: %s at %s:%d\n", Expr, File, Line);
}

_Noreturn void HalHaltSystem(ULONG ExitValue)
{
    (void)ArchIrqSave();
    HalWritePortUlong(0xF4, ExitValue); /* isa-debug-exit; absent on real hardware */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

_Noreturn void KeBugCheckEx(KAMOS_STOP_CODE Code, ULONG_PTR P1, ULONG_PTR P2, ULONG_PTR P3,
                            ULONG_PTR P4)
{
    static volatile int Entered;
    char                l1[96];
    char                l2[96];

    (void)ArchIrqSave();
    if (Entered != 0) {
        HalHaltSystem(HAL_EXIT_BUGCHECK); /* a bug check inside a bug check: stop at once */
    }
    Entered = 1;

    kprintf("KAMOS STOP 0x%08x %s P1=0x%lx P2=0x%lx P3=0x%lx P4=0x%lx\n", (unsigned)Code,
            StopName(Code), (unsigned long)P1, (unsigned long)P2, (unsigned long)P3,
            (unsigned long)P4);
    (void)RtlFormat(l1, sizeof l1, "STOP 0x%08x %s", (unsigned)Code, StopName(Code));
    (void)RtlFormat(l2, sizeof l2, "P1=0x%lx P2=0x%lx P3=0x%lx P4=0x%lx", (unsigned long)P1,
                    (unsigned long)P2, (unsigned long)P3, (unsigned long)P4);
    HalpVgaBugCheckScreen("Kamos-NT has stopped to protect itself.", l1, l2);
    HalHaltSystem(HAL_EXIT_BUGCHECK);
}

/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Fatal stop of the kernel on a broken invariant (ADR-0007).
 * Owner: A03. Contract state: provisional.
 * Stop codes are Kamos-NT values; they do not reuse Windows bug check numbers.
 */
#ifndef KAMOS_BUGCHECK_H
#define KAMOS_BUGCHECK_H

#include <kamos/types.h>

typedef enum _KAMOS_STOP_CODE {
    KAMOS_STOP_ASSERTION_FAILED     = 0x0001,
    KAMOS_STOP_UNHANDLED_EXCEPTION  = 0x0002,
    KAMOS_STOP_DOUBLE_FAULT         = 0x0003,
    KAMOS_STOP_IRQL_VIOLATION       = 0x0004,
    KAMOS_STOP_POOL_CORRUPTION      = 0x0005,
    KAMOS_STOP_BAD_BOOT_INFORMATION = 0x0006,
} KAMOS_STOP_CODE;

/*
 * Prints the stop code and parameters on the debug console, then halts every CPU.
 * Any level. Never returns. Not for recoverable errors.
 */
_Noreturn void KeBugCheckEx(KAMOS_STOP_CODE Code, ULONG_PTR P1, ULONG_PTR P2,
                            ULONG_PTR P3, ULONG_PTR P4);

void DbgPrintAssertFailure(const char *Expr, const char *File, int Line);

/*
 * Checks by build profile (RFC-0002, defect D10). KAMOS_DEBUG is set by the build.
 *  - ASSERT: a programmer-error check, active only in debug builds. In release builds the
 *    expression is not evaluated (it is still type-checked).
 *  - VERIFY: the expression is always evaluated, and checked in debug builds.
 *  - A security or integrity invariant (a bounds check on user input, a handle generation) is
 *    never an ASSERT: it calls KeBugCheckEx or returns an error explicitly, in every build.
 */
#if defined(KAMOS_DEBUG) && KAMOS_DEBUG
#define ASSERT(e)                                                              \
    do {                                                                       \
        if (!(e)) {                                                            \
            DbgPrintAssertFailure(#e, __FILE__, __LINE__);                     \
            KeBugCheckEx(KAMOS_STOP_ASSERTION_FAILED, (ULONG_PTR)__LINE__, 0,  \
                         0, 0);                                                \
        }                                                                      \
    } while (0)
#define VERIFY(e) ASSERT(e)
#else
#define ASSERT(e) ((void)sizeof(e))
#define VERIFY(e) ((void)(e))
#endif

#endif /* KAMOS_BUGCHECK_H */

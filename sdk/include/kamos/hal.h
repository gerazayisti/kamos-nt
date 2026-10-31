/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Hardware abstraction layer.
 * Owner: A03. Contract state: provisional until the Wave 1 gate.
 */
#ifndef KAMOS_HAL_H
#define KAMOS_HAL_H

#include <kamos/status.h>
#include <kamos/types.h>

/* Port I/O. Any level. */
UCHAR  HalReadPortUchar(USHORT Port);
void   HalWritePortUchar(USHORT Port, UCHAR Value);
ULONG  HalReadPortUlong(USHORT Port);
void   HalWritePortUlong(USHORT Port, ULONG Value);

/* Debug console (COM1 16550 in v0.1). Any level; never allocates. */
void   HalDebugConsoleInit(void);
void   HalDebugConsoleWrite(const char *Buffer, SIZE_T Length);

/* Interrupt state. Any level. Returns true if interrupts were enabled. */
BOOLEAN HalDisableInterrupts(void);
void    HalRestoreInterrupts(BOOLEAN WasEnabled);

/*
 * Registers a handler for an external interrupt line. PASSIVE_LEVEL only.
 * The handler runs at DEVICE_LEVEL and returns true if it serviced the interrupt.
 * Errors: STATUS_INVALID_PARAMETER (line out of range), STATUS_OBJECT_NAME_COLLISION
 * (line already owned; sharing comes with the APIC milestone).
 */
typedef BOOLEAN (*HAL_INTERRUPT_HANDLER)(void *Context);
NTSTATUS HalConnectInterrupt(ULONG Line, HAL_INTERRUPT_HANDLER Handler, void *Context);

/* Monotonic time since boot in 100 ns units. Any level. */
ULONG64 HalQueryMonotonicTime(void);

/*
 * Exit values written to the QEMU isa-debug-exit port (ADR-0010). QEMU returns
 * (ExitValue << 1) | 1, which is the code the host harness sees.
 */
#define HAL_EXIT_TESTS_PASSED  0x10u   /* QEMU exit 33. */
#define HAL_EXIT_TESTS_FAILED  0x11u   /* QEMU exit 35: tests ran and at least one failed. */
#define HAL_EXIT_BUGCHECK      0x12u   /* QEMU exit 37: KeBugCheckEx, including a failed ASSERT. */

/* Stops the machine (QEMU: isa-debug-exit with ExitValue, see ADR-0010). Never returns. */
_Noreturn void HalHaltSystem(ULONG ExitValue);

#endif /* KAMOS_HAL_H */

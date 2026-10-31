/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Execution levels (ADR-0009).
 * Owner: A05. In v0.1 A03 implements raise and lower in hal/ (software levels, ADR-0009) and
 * hands them to A05 at the start of Wave 2. CR8 is unused until the APIC milestone.
 * Contract state: provisional until the Wave 2 gate.
 */
#ifndef KAMOS_IRQL_H
#define KAMOS_IRQL_H

#include <kamos/types.h>

typedef UCHAR KIRQL;

#define PASSIVE_LEVEL  ((KIRQL)0)
#define DISPATCH_LEVEL ((KIRQL)2)
#define DEVICE_LEVEL   ((KIRQL)12)
#define HIGH_LEVEL     ((KIRQL)15)

/* Returns the current level. Any level. */
KIRQL KeGetCurrentIrql(void);

/*
 * Raises to NewIrql and returns the previous level in *OldIrql.
 * Precondition: NewIrql >= current level (asserted in debug builds). Any level.
 */
void KeRaiseIrql(KIRQL NewIrql, KIRQL *OldIrql);

/*
 * Lowers to NewIrql, delivering pending software interrupts that become unmasked.
 * Precondition: NewIrql <= current level (asserted in debug builds).
 */
void KeLowerIrql(KIRQL NewIrql);

#endif /* KAMOS_IRQL_H */

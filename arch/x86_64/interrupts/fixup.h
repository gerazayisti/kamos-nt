/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Fault fixup table lookup (ADR-0013). Pure: builds on the host (tests/hal/host/test_fixup.c).
 *
 * An entry says: a fault at exactly FaultRip resumes at ResumeRip. The match is on the exact
 * address, never on a range, so a bug elsewhere in a copy routine cannot be hidden.
 *
 * For A04: emit entries into the linker section ".kfixup" as two consecutive qwords, for example
 *     section .kfixup
 *         dq copy_fault_instruction, copy_fault_return
 * (NASM) or with a C macro that does the same. The section is read-only; the linker script
 * exports __kfixup_start and __kfixup_end.
 */
#ifndef KAMOS_FIXUP_H
#define KAMOS_FIXUP_H

#include <stddef.h>
#include <stdint.h>

typedef struct _KFIXUP_ENTRY {
    uint64_t FaultRip;
    uint64_t ResumeRip;
} KFIXUP_ENTRY;

/* Returns the entry whose FaultRip equals Rip, or NULL. */
const KFIXUP_ENTRY *KiFindFixup(const KFIXUP_ENTRY *Table, size_t Count, uint64_t Rip);

#endif /* KAMOS_FIXUP_H */

/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Fixup table lookup (ADR-0013). Pure. */

#include "fixup.h"

const KFIXUP_ENTRY *KiFindFixup(const KFIXUP_ENTRY *Table, size_t Count, uint64_t Rip)
{
    for (size_t i = 0; i < Count; i++) {
        if (Table[i].FaultRip == Rip) {
            return &Table[i];
        }
    }
    return NULL;
}

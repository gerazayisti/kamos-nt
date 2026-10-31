/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* IDT of 256 interrupt gates (Intel SDM vol. 3, 6.14.1). Interrupt gates clear IF on entry. */

#include "../../../hal/halp.h"

typedef struct __attribute__((packed)) _IDT_ENTRY {
    USHORT OffsetLow;
    USHORT Selector;
    UCHAR  Ist;
    UCHAR  TypeAttr;
    USHORT OffsetMid;
    ULONG  OffsetHigh;
    ULONG  Zero;
} IDT_ENTRY;
KAMOS_STATIC_ASSERT(sizeof(IDT_ENTRY) == 16, "IDT_ENTRY layout");

typedef struct __attribute__((packed)) _IDT_PTR {
    USHORT  Limit;
    ULONG64 Base;
} IDT_PTR;

static IDT_ENTRY Idt[256] __attribute__((aligned(16)));

static UCHAR IstFor(ULONG Vector)
{
    switch (Vector) {
    case 8:
        return 1; /* double fault */
    case 2:
        return 2; /* NMI */
    case 18:
        return 3; /* machine check */
    default:
        return 0;
    }
}

void ArchInitIdt(void)
{
    IDT_PTR ptr;

    for (ULONG v = 0; v < 256; v++) {
        ULONG64 h = KiIsrTable[v];

        Idt[v].OffsetLow  = (USHORT)h;
        Idt[v].Selector   = KI_SELECTOR_KCODE;
        Idt[v].Ist        = IstFor(v);
        Idt[v].TypeAttr   = 0x8E; /* present, DPL 0, 64-bit interrupt gate */
        Idt[v].OffsetMid  = (USHORT)(h >> 16);
        Idt[v].OffsetHigh = (ULONG)(h >> 32);
        Idt[v].Zero       = 0;
    }
    ptr.Limit = sizeof Idt - 1;
    ptr.Base  = (ULONG64)(uintptr_t)Idt;
    __asm__ volatile("lidt %0" : : "m"(ptr) : "memory");
}

/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Port I/O (hal.h) and CPU feature setup. Sources: Intel SDM vol. 2 (IN, OUT, CPUID), vol. 3 ch. 2.
 */

#include "../../../hal/halp.h"

UCHAR HalReadPortUchar(USHORT Port)
{
    UCHAR v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(Port));
    return v;
}

void HalWritePortUchar(USHORT Port, UCHAR Value)
{
    __asm__ volatile("outb %0, %1" : : "a"(Value), "Nd"(Port));
}

ULONG HalReadPortUlong(USHORT Port)
{
    ULONG v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(Port));
    return v;
}

void HalWritePortUlong(USHORT Port, ULONG Value)
{
    __asm__ volatile("outl %0, %1" : : "a"(Value), "Nd"(Port));
}

ULONG64 ArchInitCpuFeatures(void)
{
    ULONG   maxLeaf, a, b, c, d;
    ULONG64 set = 0;

    __asm__ volatile("cpuid" : "=a"(maxLeaf), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    if (maxLeaf >= 7) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
        if ((b & (1u << 7)) != 0) {
            set |= CR4_SMEP;
        }
        if ((b & (1u << 20)) != 0) {
            set |= CR4_SMAP;
        }
    }
    if (set != 0) {
        ArchWriteCr4(ArchReadCr4() | set);
    }
    return set;
}

/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * GDT and TSS (Intel SDM vol. 3, 3.4.5, 7.2.3, 6.14.5). Selector layout, chosen so that
 * STAR[63:48] = 0x18 gives SYSRET the user selectors it needs (A07):
 *   0x00 null, 0x08 kernel code64, 0x10 kernel data, 0x18 user code32 (unused),
 *   0x20 user data (0x23), 0x28 user code64 (0x2B), 0x30 TSS (16 bytes).
 * IST1 = double fault, IST2 = NMI, IST3 = machine check; RSP0 = boot stack until A05 has threads.
 */

#include "../../../hal/halp.h"

typedef struct __attribute__((packed)) _TSS64 {
    ULONG   Reserved0;
    ULONG64 Rsp[3];
    ULONG64 Reserved1;
    ULONG64 Ist[7];
    ULONG64 Reserved2;
    USHORT  Reserved3;
    USHORT  IoMapBase;
} TSS64;
KAMOS_STATIC_ASSERT(sizeof(TSS64) == 104, "TSS64 layout");

typedef struct __attribute__((packed)) _DESCRIPTOR_PTR {
    USHORT  Limit;
    ULONG64 Base;
} DESCRIPTOR_PTR;

extern char KiBootStackTop[];

UCHAR KiDoubleFaultStack[KI_IST_STACK_SIZE] __attribute__((aligned(16)));
UCHAR KiNmiStack[KI_IST_STACK_SIZE] __attribute__((aligned(16)));
UCHAR KiMachineCheckStack[KI_IST_STACK_SIZE] __attribute__((aligned(16)));

static TSS64   Tss __attribute__((aligned(16)));
static ULONG64 Gdt[8] __attribute__((aligned(16)));

void ArchInitGdt(void)
{
    DESCRIPTOR_PTR ptr;
    ULONG64        base  = (ULONG64)(uintptr_t)&Tss;
    ULONG64        limit = sizeof Tss - 1;

    Gdt[0] = 0;
    Gdt[1] = 0x00209A0000000000ull; /* kernel code, L=1 */
    Gdt[2] = 0x0000920000000000ull; /* kernel data */
    Gdt[3] = 0x00CFFA000000FFFFull; /* user code32 */
    Gdt[4] = 0x00CFF2000000FFFFull; /* user data */
    Gdt[5] = 0x0020FA0000000000ull; /* user code64 */
    Gdt[6] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ull << 40) |
             (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
    Gdt[7] = base >> 32;

    Tss.Rsp[0]    = (ULONG64)(uintptr_t)KiBootStackTop;
    Tss.Ist[0]    = (ULONG64)(uintptr_t)KiDoubleFaultStack + KI_IST_STACK_SIZE;
    Tss.Ist[1]    = (ULONG64)(uintptr_t)KiNmiStack + KI_IST_STACK_SIZE;
    Tss.Ist[2]    = (ULONG64)(uintptr_t)KiMachineCheckStack + KI_IST_STACK_SIZE;
    Tss.IoMapBase = sizeof Tss; /* beyond the limit: no I/O permission bitmap */

    ptr.Limit = sizeof Gdt - 1;
    ptr.Base  = (ULONG64)(uintptr_t)Gdt;
    __asm__ volatile("lgdt %0" : : "m"(ptr) : "memory");
    __asm__ volatile("pushq $0x08\n\t"
                     "leaq 1f(%%rip), %%rax\n\t"
                     "pushq %%rax\n\t"
                     "lretq\n"
                     "1:\n\t"
                     "movw $0x10, %%ax\n\t"
                     "movw %%ax, %%ds\n\t"
                     "movw %%ax, %%es\n\t"
                     "movw %%ax, %%ss\n\t"
                     "xorl %%eax, %%eax\n\t"
                     "movw %%ax, %%fs\n\t"
                     "movw %%ax, %%gs\n"
                     :
                     :
                     : "rax", "memory");
    __asm__ volatile("ltr %0" : : "r"((USHORT)KI_SELECTOR_TSS));
}

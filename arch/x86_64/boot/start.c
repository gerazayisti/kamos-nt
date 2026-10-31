/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* First C code of the kernel (entry state: docs/architecture/boot-contract.md). */

#include "../../../hal/halp.h"

ULONG KiBootMagic;
ULONG KiBootMbiPhysical;

extern ULONG64 KiPtLow[512];
extern char    __kernel_phys_start[];
extern char    __kernel_phys_end[];

#define PTE_PRESENT 0x1ull
#define PTE_NX      (1ull << 63)

void ArchFinishBootMappings(void)
{
    ULONG64 firstImagePage = (ULONG64)(uintptr_t)__kernel_phys_start >> 12;
    ULONG64 endImagePage   = ((ULONG64)(uintptr_t)__kernel_phys_end + 0xFFF) >> 12;

    for (ULONG64 i = 0; i < 512; i++) {
        if (i >= firstImagePage && i < endImagePage) {
            KiPtLow[i] = 0; /* no writable alias of .text (W^X) */
        } else if ((KiPtLow[i] & PTE_PRESENT) != 0) {
            KiPtLow[i] |= PTE_NX;
        }
    }
    ArchWriteCr3(ArchReadCr3()); /* flush the whole TLB */
}

_Noreturn void KiStartKernel(ULONG MultibootMagic, ULONG MbiPhysical)
{
    KiBootMagic       = MultibootMagic;
    KiBootMbiPhysical = MbiPhysical;

    HalDebugConsoleInit();
    kprintf("Kamos-NT kernel 0.1.0 (%s) x86_64\n", KAMOS_DEBUG ? "debug" : "release");
    ArchFinishBootMappings();
    (void)ArchInitCpuFeatures();
    HalInitializeBootInformation(MultibootMagic, MbiPhysical);

    KtMain();
}

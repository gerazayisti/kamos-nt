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
    ArchInitGdt();
    ArchInitIdt(); /* from here on a fault prints a line instead of resetting the machine */
    /*
     * The loader may place its information structure in a gap between two segments of the image
     * (seen in the release build: MBI at 0x10d5a0, inside [__kernel_phys_start,
     * __kernel_phys_end)), so it must be read through the identity map BEFORE
     * ArchFinishBootMappings removes the alias.
     */
    HalInitializeBootInformation(MultibootMagic, MbiPhysical);
    ArchFinishBootMappings();
    (void)ArchInitCpuFeatures();
    HalpInitPic();
    HalpInitTimer(); /* leaves interrupts enabled at PASSIVE_LEVEL */
    kprintf("TSC %lu Hz (calibrated on interrupt-driven PIT ticks)\n", (unsigned long)HalpTscHz());
    kprintf("calibration: %lu outlier window(s) of 91 ignored\n", (unsigned long)HalpLostTicks);

    KtMain();
}

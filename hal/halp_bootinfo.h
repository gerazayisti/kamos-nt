/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Interface of the pure Multiboot2 parser (hal/bootinfo.c). Builds on the host as well. */
#ifndef KAMOS_HALP_BOOTINFO_H
#define KAMOS_HALP_BOOTINFO_H

#include <kamos/boot.h>
#include <kamos/types.h>

typedef enum _HALP_BOOTINFO_RESULT {
    BootInfoOk = 0,
    BootInfoBadMagic,
    BootInfoBadPointer,
    BootInfoBadSize,
    BootInfoBadTag,
    BootInfoNoEndTag,
    BootInfoBadMemoryMap,
    BootInfoTooManyRegions,
    BootInfoWrap,
    BootInfoOverlap,
    BootInfoNoUsableMemory,
    BootInfoTooManyModules,
    BootInfoBadModule,
    BootInfoBadFramebuffer,
    BootInfoBadRsdp,
} HALP_BOOTINFO_RESULT;

/* Largest boot information structure accepted (bytes). */
#define HALP_BOOTINFO_MAX_SIZE (256u * 1024u)

/*
 * Validates the Multiboot2 structure at Mbi (AvailableBytes readable) and fills *Out.
 * MbiPhysical is the physical address of Mbi (8-byte aligned). KernelStart and KernelEnd
 * (physical, End exclusive; End <= Start to skip) are a range no module may overlap.
 * Pure: reads only the structure, calls nothing, never stops the kernel.
 */
HALP_BOOTINFO_RESULT HalpParseMultiboot2(const void *Mbi, ULONG64 MbiPhysical,
                                         ULONG64 AvailableBytes, ULONG64 KernelStart,
                                         ULONG64 KernelEnd, BOOT_INFORMATION *Out);
const char          *HalpBootInfoResultName(HALP_BOOTINFO_RESULT Result);

#endif /* KAMOS_HALP_BOOTINFO_H */

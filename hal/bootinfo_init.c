/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Validates the loader's boot information once and keeps the copy (boot.h). */

#include "halp.h"

extern char __kernel_phys_start[];
extern char __kernel_phys_end[];

static BOOT_INFORMATION BootInfo;
static BOOLEAN          BootInfoReady;

const BOOT_INFORMATION *HalGetBootInformation(void)
{
    if (!BootInfoReady) {
        KeBugCheckEx(KAMOS_STOP_BAD_BOOT_INFORMATION, 0xFFFF, 0, 0, 0);
    }
    return &BootInfo;
}

/*
 * The RSDP in the loader's tag is a copy inside memory that is later reused. Find the firmware's
 * original in the BIOS read-only area (ACPI 6 section 5.2.5.1: 16-byte aligned, 0xE0000 to
 * 0xFFFFF) so that the address stays valid. The EBDA is not scanned: its pointer lives in the
 * first 64 KiB, which is unmapped (null guard, ADR-0005). 0 when no match.
 */
static ULONG64 FindFirmwareRsdp(ULONG64 CopyPhysical)
{
    const uint8_t *copy = (const uint8_t *)(uintptr_t)CopyPhysical;

    for (ULONG64 a = 0xE0000; a < 0x100000; a += 16) {
        const uint8_t *p    = (const uint8_t *)(uintptr_t)a;
        BOOLEAN        same = true;
        for (unsigned i = 0; i < 20; i++) {
            if (p[i] != copy[i]) {
                same = false;
                break;
            }
        }
        if (same) {
            return a;
        }
    }
    return 0;
}

void HalInitializeBootInformation(ULONG Magic, ULONG MbiPhysical)
{
    HALP_BOOTINFO_RESULT r;
    ULONG64              avail = 0x100000000ull - MbiPhysical; /* the identity map covers 4 GiB */

    if (Magic != 0x36D76289u) {
        r = BootInfoBadMagic;
    } else {
        r = HalpParseMultiboot2((const void *)(uintptr_t)MbiPhysical, MbiPhysical,
                                avail > HALP_BOOTINFO_MAX_SIZE ? HALP_BOOTINFO_MAX_SIZE : avail,
                                (ULONG64)(uintptr_t)__kernel_phys_start,
                                (ULONG64)(uintptr_t)__kernel_phys_end, &BootInfo);
    }
    if (r != BootInfoOk) {
        kprintf("BOOTINFO rejected: %s (mbi=0x%x)\n", HalpBootInfoResultName(r), MbiPhysical);
        KeBugCheckEx(KAMOS_STOP_BAD_BOOT_INFORMATION, (ULONG_PTR)r, MbiPhysical, Magic, 0);
    }
    if (BootInfo.AcpiRsdp != 0) {
        BootInfo.AcpiRsdp = FindFirmwareRsdp(BootInfo.AcpiRsdp);
    }
    BootInfoReady = true;
}

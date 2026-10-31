/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * QEMU fw_cfg, I/O port interface (QEMU docs/specs/fw_cfg.rst): selector 0x510, data 0x511.
 * Used only to pick a test scenario (the file opt/kamos/test). Absent on real hardware: the
 * signature is checked first, and a missing device reads as 0xFF.
 */

#include "halp.h"

#define FWCFG_SEL      0x510u
#define FWCFG_DATA     0x511u
#define FWCFG_SIG      0x0000u
#define FWCFG_FILE_DIR 0x0019u

static void Select(USHORT Key)
{
    __asm__ volatile("outw %0, %1" : : "a"(Key), "Nd"((USHORT)FWCFG_SEL)); /* 16-bit write */
}

static UCHAR Byte(void)
{
    return HalReadPortUchar(FWCFG_DATA);
}

static ULONG Be32(void)
{
    ULONG v = 0;
    for (int i = 0; i < 4; i++) {
        v = (v << 8) | Byte();
    }
    return v;
}

int HalpFwCfgReadFile(const char *Name, char *Buffer, ULONG BufferSize)
{
    ULONG count;

    if (BufferSize == 0) {
        return -1;
    }
    Select(FWCFG_SIG);
    if (Byte() != 'Q' || Byte() != 'E' || Byte() != 'M' || Byte() != 'U') {
        return -2; /* no fw_cfg device */
    }
    Select(FWCFG_FILE_DIR);
    count = Be32();
    if (count > 4096) {
        return -1;
    }
    for (ULONG i = 0; i < count; i++) {
        ULONG  size = Be32();
        USHORT sel;
        char   entryName[56];

        sel = (USHORT)((ULONG)Byte() << 8);
        sel = (USHORT)(sel | Byte());
        (void)Byte();
        (void)Byte(); /* reserved */
        for (int k = 0; k < 56; k++) {
            entryName[k] = (char)Byte();
        }
        entryName[55] = '\0';
        if (memcmp(entryName, Name, strlen(Name) + 1) == 0) {
            ULONG n = size < BufferSize - 1 ? size : BufferSize - 1;
            Select(sel);
            for (ULONG k = 0; k < n; k++) {
                Buffer[k] = (char)Byte();
            }
            Buffer[n] = '\0';
            return (int)n;
        }
    }
    return -1;
}

/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Debug console on COM1 (16550, 115200 8N1, polled) and the VGA text screen used by the bug check.
 * Sources: 16550 data sheet register map; IBM VGA text mode (0xB8000, attribute byte).
 * Nothing allocates. A line written by one call is not interleaved with another (interrupts off).
 */

#include "halp.h"

#define COM1     0x3F8u
#define COM_DATA 0
#define COM_IER  1
#define COM_FCR  2
#define COM_LCR  3
#define COM_MCR  4
#define COM_LSR  5

void HalDebugConsoleInit(void)
{
    HalWritePortUchar(COM1 + COM_IER, 0x00);  /* no UART interrupts */
    HalWritePortUchar(COM1 + COM_LCR, 0x80);  /* DLAB */
    HalWritePortUchar(COM1 + COM_DATA, 0x01); /* divisor 1: 115200 baud */
    HalWritePortUchar(COM1 + COM_IER, 0x00);
    HalWritePortUchar(COM1 + COM_LCR, 0x03); /* 8 data bits, no parity, 1 stop bit */
    HalWritePortUchar(COM1 + COM_FCR, 0xC7); /* FIFO on, cleared, 14-byte trigger */
    HalWritePortUchar(COM1 + COM_MCR, 0x03); /* DTR, RTS (no loopback, no OUT2) */
}

static void PutByte(UCHAR C)
{
    /* Bounded wait: a machine without a UART must not hang the kernel. */
    for (ULONG i = 0; i < 200000; i++) {
        if ((HalReadPortUchar(COM1 + COM_LSR) & 0x20) != 0) {
            HalWritePortUchar(COM1 + COM_DATA, C);
            return;
        }
    }
}

void HalDebugConsoleWrite(const char *Buffer, SIZE_T Length)
{
    ULONG64 flags = ArchIrqSave();

    for (SIZE_T i = 0; i < Length; i++) {
        if (Buffer[i] == '\n' && (i == 0 || Buffer[i - 1] != '\r')) {
            PutByte('\r');
        }
        PutByte((UCHAR)Buffer[i]);
    }
    ArchIrqRestore(flags);
}

int kprintf(const char *Format, ...)
{
    char    buf[256];
    va_list ap;
    size_t  n;

    va_start(ap, Format);
    n = RtlFormatV(buf, sizeof buf, Format, ap);
    va_end(ap);
    if (n >= sizeof buf) {
        n = sizeof buf - 1;
    }
    HalDebugConsoleWrite(buf, n);
    return (int)n;
}

/* kt_sink for tests/framework/include/ktest.h. */
void kt_sink(const char *buf, size_t len)
{
    HalDebugConsoleWrite(buf, len);
}

#define VGA_BASE ((volatile uint16_t *)0xB8000)
#define VGA_COLS 80u
#define VGA_ROWS 25u
#define VGA_ATTR 0x1F00u /* white on blue */

static void VgaText(unsigned Row, const char *Text)
{
    for (unsigned c = 0; c < VGA_COLS; c++) {
        VGA_BASE[Row * VGA_COLS + c] = (uint16_t)(VGA_ATTR | ' ');
    }
    for (unsigned c = 0; c < VGA_COLS && *Text != '\0'; c++, Text++) {
        VGA_BASE[Row * VGA_COLS + c] = (uint16_t)(VGA_ATTR | (UCHAR)*Text);
    }
}

void HalpVgaBugCheckScreen(const char *Title, const char *Line1, const char *Line2)
{
    for (unsigned r = 0; r < VGA_ROWS; r++) {
        VgaText(r, "");
    }
    VgaText(1, Title);
    VgaText(3, Line1);
    VgaText(4, Line2);
}

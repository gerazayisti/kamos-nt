/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * A02 build fixture, C part. Checks that C compiled with KCFLAGS runs after the long-mode
 * switch: .data and .bss are loaded, and a 128-bit division (libgcc, no red zone) is right.
 * Expected values come from Python: (2**100 + 12345) divmod 1000003.
 */
#include <kamos/types.h>

static uint8_t bss_probe[64];
static volatile ULONG data_probe = 0xC0FFEE42u;

static void outb(uint16_t port, uint8_t v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static void puts_com1(const char *s)
{
    while (*s) {
        outb(0x3F8, (uint8_t)*s++);
    }
}

static void put_result(const char *name, int ok)
{
    puts_com1("KTEST fixture ");
    puts_com1(name);
    puts_com1(ok ? " PASS\r\n" : " FAIL\r\n");
}

void fixture_main(void)
{
    int fail = 0;
    int pass = 0;
    int ok;

    ok = (data_probe == 0xC0FFEE42u);
    put_result("data", ok);
    ok ? pass++ : fail++;

    ok = 1;
    for (unsigned i = 0; i < sizeof(bss_probe); i++) {
        if (bss_probe[i] != 0) {
            ok = 0;
        }
    }
    put_result("bss", ok);
    ok ? pass++ : fail++;

    volatile unsigned __int128 a = ((unsigned __int128)1 << 100) + 12345u;
    volatile unsigned __int128 b = 1000003u;
    unsigned __int128 q = a / b;
    unsigned __int128 r = a % b;
    ok = ((uint64_t)(q >> 64) == 0x10c6fULL) && ((uint64_t)q == 0x45449cb59c68de59ULL) &&
         ((uint64_t)r == 265454ULL);
    put_result("udivti3", ok);
    ok ? pass++ : fail++;

    puts_com1("KTEST-END pass=");
    outb(0x3F8, (uint8_t)('0' + pass));
    puts_com1(" fail=");
    outb(0x3F8, (uint8_t)('0' + fail));
    puts_com1(" skip=0\r\n");
    __asm__ volatile("outl %0, $0xf4" : : "a"((uint32_t)(fail ? 0x11 : 0x10)));
    for (;;) {
        __asm__ volatile("hlt");
    }
}

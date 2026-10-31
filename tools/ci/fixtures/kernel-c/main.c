/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * A02 build fixture, C part. Checks that C compiled with KCFLAGS runs after the long-mode
 * switch: .data and .bss are loaded, and a 128-bit division (libgcc, no red zone) is right.
 * Expected values come from Python: (2**100 + 12345) divmod 1000003.
 */
#include <kamos/bugcheck.h>
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

/* Stand-ins for the A03 functions that ASSERT calls (bugcheck.h). Stop code 0x12 = QEMU exit 37. */
void DbgPrintAssertFailure(const char *Expr, const char *File, int Line)
{
    (void)Line;
    puts_com1("ASSERT failed: ");
    puts_com1(Expr);
    puts_com1(" in ");
    puts_com1(File);
    puts_com1("\r\n");
}

_Noreturn void KeBugCheckEx(KAMOS_STOP_CODE Code, ULONG_PTR P1, ULONG_PTR P2, ULONG_PTR P3,
                            ULONG_PTR P4)
{
    (void)Code; (void)P1; (void)P2; (void)P3; (void)P4;
    puts_com1("BUGCHECK\r\n");
    __asm__ volatile("outl %0, $0xf4" : : "a"((uint32_t)0x12));
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* ASSERT must not evaluate its expression in release; VERIFY always does. */
static volatile int assert_condition = 1;
static volatile int assert_evaluated;
static volatile int verify_evaluated;

static int check_assert(void) { assert_evaluated++; return assert_condition; }
static int check_verify(void) { verify_evaluated++; return 1; }

__attribute__((noinline)) void assert_probe(void)
{
    ASSERT(check_assert());
    VERIFY(check_verify());
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

#ifdef FIXTURE_FAIL_ASSERT
    assert_condition = 0;   /* debug: bugcheck, QEMU exit 37. release: not evaluated, exit 33 */
#endif
    assert_probe();
    ok = (verify_evaluated == 1) && (assert_evaluated == (KAMOS_DEBUG ? 1 : 0));
    put_result(KAMOS_DEBUG ? "profile-debug" : "profile-release", ok);
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

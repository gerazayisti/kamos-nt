/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Host tests of the kernel formatter (ntoskrnl/rtl/format.c). Oracle: the host snprintf for the
 * supported subset, and hand-written strings for the extensions (%p form, truncation return).
 */
#define KTEST_HOST 1
#include <ktest.h>
#include <stdio.h>
#include <string.h>

#include "../../../ntoskrnl/rtl/rtl.h"

#define SAME(fmt, ...)                                                                             \
    do {                                                                                           \
        char   a[128], b[128];                                                                     \
        int    na = snprintf(a, sizeof a, fmt, __VA_ARGS__);                                       \
        size_t nb = RtlFormat(b, sizeof b, fmt, __VA_ARGS__);                                      \
        KT_CHECK_EQ((size_t)na, nb);                                                               \
        KT_CHECK(strcmp(a, b) == 0);                                                               \
    } while (0)

static void test_integers(void)
{
    SAME("%d %d %d", 0, -1, 2147483647);
    SAME("%u %u", 0u, 4294967295u);
    SAME("%x %X %#x %#X", 0xdeadu, 0xdeadu, 0xdeadu, 0xdeadu);
    SAME("%ld %lu %lx", -9223372036854775807L, 18446744073709551615UL, 0xfedcba9876543210UL);
    SAME("%llx %llu", 0x1ULL, 0xffffffffffffffffULL);
    SAME("%zu %zx", (size_t)77, (size_t)255);
}

static void test_width_and_flags(void)
{
    SAME("[%5d] [%-5d] [%05d] [%5s] [%-5s]|", 42, 42, 42, "ab", "ab");
    SAME("[%08x] [%-8x] [%8x]", 0xabcu, 0xabcu, 0xabcu);
    SAME("[%016lx]", 0x1234UL);
    SAME("[%*d] [%-*d]", 6, 7, 6, 7);
    SAME("[%05d]", -42);
    SAME("[%3c] [%-3c]", 'x', 'y');
    SAME("%s%%%s", "a", "b");
}

/* No format attribute here: the cases below are deliberately odd. */
static size_t Fmt(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    size_t  n;

    va_start(ap, fmt);
    n = RtlFormatV(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

static void test_extensions(void)
{
    char   b[64];
    size_t n;

    n = Fmt(b, sizeof b, "%p", (void *)0x1000);
    KT_CHECK(strcmp(b, "0x0000000000001000") == 0);
    KT_CHECK_EQ(n, 18);
    (void)Fmt(b, sizeof b, "%s", (const char *)NULL);
    KT_CHECK(strcmp(b, "(null)") == 0);
    (void)Fmt(b, sizeof b, "%q");
    KT_CHECK(strcmp(b, "%q") == 0); /* unknown conversion copied */
    (void)Fmt(b, sizeof b, "100%");
    KT_CHECK(strcmp(b, "100%") == 0); /* a lone percent at the end */
}

static void test_truncation(void)
{
    char   b[8];
    size_t n = Fmt(b, sizeof b, "%s", "0123456789");

    KT_CHECK_EQ(n, 10);                  /* the length the whole output would have */
    KT_CHECK(strcmp(b, "0123456") == 0); /* terminated */
    n = Fmt(b, 1, "abc");
    KT_CHECK_EQ(n, 3);
    KT_CHECK_EQ(b[0], 0);
    n = Fmt(NULL, 0, "abc");
    KT_CHECK_EQ(n, 3);
}

static void test_no_overrun(void)
{
    /* Guard bytes after the buffer must stay intact for every capacity. */
    for (size_t cap = 1; cap < 24; cap++) {
        unsigned char buf[32];
        memset(buf, 0x5A, sizeof buf);
        (void)Fmt((char *)buf, cap, "%d:%s:%lx", -12345, "text", 0xabcdefUL);
        for (size_t i = cap; i < sizeof buf; i++) {
            KT_REQUIRE_EQ(buf[i], 0x5A);
        }
        KT_REQUIRE(strlen((char *)buf) <= cap - 1); /* always terminated inside the capacity */
    }
}

int main(void)
{
    static const kt_case cases[] = {
        KT_CASE("rtl", test_integers),   KT_CASE("rtl", test_width_and_flags),
        KT_CASE("rtl", test_extensions), KT_CASE("rtl", test_truncation),
        KT_CASE("rtl", test_no_overrun),
    };
    return kt_run(cases, sizeof cases / sizeof cases[0]) ? 1 : 0;
}

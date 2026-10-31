/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Test framework: assertion macros and the serial protocol of ADR-0010.
 * Owner: A10. Same header for host tests and kernel tests.
 *
 * Output, one line per result (docs/testing/oracles-wave1.md, section 1):
 *     KTEST <suite> <name> PASS|FAIL|SKIP [detail]
 *     KTEST-END pass=<n> fail=<n> skip=<n>
 *
 * Host build: define KTEST_HOST; lines go to standard output and kt_run returns the number of
 * failures. Kernel build: do not define it; the caller provides
 *     void kt_sink(const char *buf, size_t len);
 * (for example a call to HalDebugConsoleWrite). Only <stddef.h> and <stdint.h> are used, so the
 * header compiles freestanding. Nothing allocates.
 *
 * KT_CHECK records a failure and goes on. KT_REQUIRE records it and returns from the test
 * function, so a test function must return void and use these macros at its own level.
 * Only the first failure of a test is reported in its detail.
 */
#ifndef KAMOS_KTEST_H
#define KAMOS_KTEST_H

#include <stddef.h>
#include <stdint.h>

#ifdef KTEST_HOST
#include <stdio.h>
#else
void kt_sink(const char *buf, size_t len);
#endif

typedef void (*kt_test_fn)(void);

typedef struct kt_case {
    const char *suite;
    const char *name;
    kt_test_fn  fn;
} kt_case;

typedef struct kt_state {
    unsigned pass, fail, skip;
    int      cur_failed;     /* the running test has failed */
    int      cur_skipped;
    char     detail[160];    /* first failure of the running test */
} kt_state;

static kt_state kt__st;

static inline void kt__put(const char *s, size_t n)
{
#ifdef KTEST_HOST
    fwrite(s, 1, n, stdout);
#else
    kt_sink(s, n);
#endif
}

/* Appends to a bounded buffer; never overflows, always terminated. */
typedef struct kt__buf { char *p; size_t cap, len; } kt__buf;

static inline void kt__cat(kt__buf *b, const char *s)
{
    while (*s && b->len + 1 < b->cap) {
        char c = *s++;
        b->p[b->len++] = (c == '\n' || c == '\r') ? ' ' : c;   /* one line only */
    }
    b->p[b->len] = '\0';
}

static inline void kt__cat_u64(kt__buf *b, uint64_t v, int hex)
{
    char tmp[24];
    int  i = 0;
    unsigned base = hex ? 16u : 10u;
    if (hex)
        kt__cat(b, "0x");
    do {
        unsigned d = (unsigned)(v % base);
        tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
        v /= base;
    } while (v != 0 && i < (int)sizeof tmp);
    char out[24];
    int  j = 0;
    while (i > 0)
        out[j++] = tmp[--i];
    out[j] = '\0';
    kt__cat(b, out);
}

static inline const char *kt__base(const char *path)
{
    const char *r = path;
    for (; *path; path++)
        if (*path == '/' || *path == '\\')
            r = path + 1;
    return r;
}

static inline void kt__fail(const char *file, int line, const char *msg, int has_vals,
                            uint64_t a, uint64_t b)
{
    kt__st.cur_failed = 1;
    if (kt__st.detail[0] != '\0')
        return;
    kt__buf bf = { kt__st.detail, sizeof kt__st.detail, 0 };
    kt__cat(&bf, kt__base(file));
    kt__cat(&bf, ":");
    kt__cat_u64(&bf, (uint64_t)line, 0);
    kt__cat(&bf, " ");
    kt__cat(&bf, msg);
    if (has_vals) {
        kt__cat(&bf, " (");
        kt__cat_u64(&bf, a, 1);
        kt__cat(&bf, " vs ");
        kt__cat_u64(&bf, b, 1);
        kt__cat(&bf, ")");
    }
}

#define KT_CHECK(cond) \
    do { if (!(cond)) kt__fail(__FILE__, __LINE__, #cond, 0, 0, 0); } while (0)
#define KT_CHECK_EQ(a, b) \
    do { uint64_t kt_a_ = (uint64_t)(a), kt_b_ = (uint64_t)(b); \
         if (kt_a_ != kt_b_) kt__fail(__FILE__, __LINE__, #a " == " #b, 1, kt_a_, kt_b_); \
    } while (0)
#define KT_REQUIRE(cond) \
    do { if (!(cond)) { kt__fail(__FILE__, __LINE__, #cond, 0, 0, 0); return; } } while (0)
#define KT_REQUIRE_EQ(a, b) \
    do { uint64_t kt_a_ = (uint64_t)(a), kt_b_ = (uint64_t)(b); \
         if (kt_a_ != kt_b_) { kt__fail(__FILE__, __LINE__, #a " == " #b, 1, kt_a_, kt_b_); return; } \
    } while (0)
#define KT_FAIL(msg) \
    do { kt__fail(__FILE__, __LINE__, (msg), 0, 0, 0); return; } while (0)
/* Skips the running test; the reason is mandatory and appears in the line. */
#define KT_SKIP(reason) \
    do { kt__st.cur_skipped = 1; kt__st.detail[0] = '\0'; \
         kt__buf kt_b_ = { kt__st.detail, sizeof kt__st.detail, 0 }; \
         kt__cat(&kt_b_, (reason)); return; } while (0)

/* Emits a result line for a measurement the runner reads: key=value in the detail. */
static inline void kt_emit(const char *suite, const char *name, const char *result,
                           const char *key, uint64_t value)
{
    char line[200];
    kt__buf b = { line, sizeof line, 0 };
    line[0] = '\0';
    kt__cat(&b, "KTEST ");
    kt__cat(&b, suite);
    kt__cat(&b, " ");
    kt__cat(&b, name);
    kt__cat(&b, " ");
    kt__cat(&b, result);
    kt__cat(&b, " ");
    kt__cat(&b, key);
    kt__cat(&b, "=");
    kt__cat_u64(&b, value, 0);
    kt__put(line, b.len);
    kt__put("\n", 1);
}

/* Runs the table, prints the lines and the END line. Returns the number of failures. */
static inline unsigned kt_run(const kt_case *cases, size_t n)
{
    kt__st.pass = kt__st.fail = kt__st.skip = 0;
    for (size_t i = 0; i < n; i++) {
        char line[260];
        kt__buf b = { line, sizeof line, 0 };
        kt__st.cur_failed = kt__st.cur_skipped = 0;
        kt__st.detail[0] = '\0';
        cases[i].fn();
        const char *res = kt__st.cur_failed ? "FAIL" : (kt__st.cur_skipped ? "SKIP" : "PASS");
        if (kt__st.cur_failed) kt__st.fail++;
        else if (kt__st.cur_skipped) kt__st.skip++;
        else kt__st.pass++;
        line[0] = '\0';
        kt__cat(&b, "KTEST ");
        kt__cat(&b, cases[i].suite);
        kt__cat(&b, " ");
        kt__cat(&b, cases[i].name);
        kt__cat(&b, " ");
        kt__cat(&b, res);
        if ((kt__st.cur_failed || kt__st.cur_skipped) && kt__st.detail[0]) {
            kt__cat(&b, " ");
            kt__cat(&b, kt__st.detail);
        }
        kt__put(line, b.len);
        kt__put("\n", 1);
    }
    char end[80];
    kt__buf e = { end, sizeof end, 0 };
    end[0] = '\0';
    kt__cat(&e, "KTEST-END pass=");
    kt__cat_u64(&e, kt__st.pass, 0);
    kt__cat(&e, " fail=");
    kt__cat_u64(&e, kt__st.fail, 0);
    kt__cat(&e, " skip=");
    kt__cat_u64(&e, kt__st.skip, 0);
    kt__put(end, e.len);
    kt__put("\n", 1);
    return kt__st.fail;
}

#define KT_CASE(suite, fn) { (suite), #fn, (fn) }

#endif /* KAMOS_KTEST_H */

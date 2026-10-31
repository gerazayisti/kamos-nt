/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Interface between the test runner (ktests.c, the only file that includes ktest.h, because that
 * header keeps its state in a static variable) and the test files of Phase 1 and 2.
 */
#ifndef KAMOS_KTEST_INTERNAL_H
#define KAMOS_KTEST_INTERNAL_H

#include "../halp.h"

typedef struct _KTX_CASE {
    const char *Suite;
    const char *Name;
    void (*Fn)(void);
} KTX_CASE;

typedef struct _KTX_TABLE {
    const KTX_CASE *Cases;
    size_t          Count;
} KTX_TABLE;

/* Provided by ktests.c. */
void KtFail(const char *File, int Line, const char *Msg, int HasVals, uint64_t A, uint64_t B);
void KtSkip(const char *Reason);
void KtNoteU64(const char *Key, uint64_t Value);
void KtNoteHex(const char *Key, uint64_t Value);
void KtNoteStr(const char *Key, const char *Value);
void KtPrintLine(const char *Suite, const char *Name, const char *Result, const char *Detail);

#define KTX_CHECK(cond)                                                                            \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            KtFail(__FILE__, __LINE__, #cond, 0, 0, 0);                                            \
        }                                                                                          \
    } while (0)
#define KTX_CHECK_EQ(a, b)                                                                         \
    do {                                                                                           \
        uint64_t ktxA = (uint64_t)(a), ktxB = (uint64_t)(b);                                       \
        if (ktxA != ktxB) {                                                                        \
            KtFail(__FILE__, __LINE__, #a " == " #b, 1, ktxA, ktxB);                               \
        }                                                                                          \
    } while (0)
#define KTX_REQUIRE(cond)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            KtFail(__FILE__, __LINE__, #cond, 0, 0, 0);                                            \
            return;                                                                                \
        }                                                                                          \
    } while (0)
#define KTX_REQUIRE_EQ(a, b)                                                                       \
    do {                                                                                           \
        uint64_t ktxA = (uint64_t)(a), ktxB = (uint64_t)(b);                                       \
        if (ktxA != ktxB) {                                                                        \
            KtFail(__FILE__, __LINE__, #a " == " #b, 1, ktxA, ktxB);                               \
            return;                                                                                \
        }                                                                                          \
    } while (0)
#define KTX_SKIP(reason)                                                                           \
    do {                                                                                           \
        KtSkip(reason);                                                                            \
        return;                                                                                    \
    } while (0)

/* Test tables. */
extern const KTX_CASE KtBootCases[];
extern const size_t   KtBootCaseCount;

extern const KTX_CASE KtExcCases[];
extern const size_t   KtExcCaseCount;
extern const KTX_CASE KtIrqlCases[];
extern const size_t   KtIrqlCaseCount;

/* Scenarios that end in a bug check; each runs in its own boot. */
void KtScenarioFellThrough(const char *Name);
void KtScenarioPfNoFixup(void);
void KtScenarioPfOffByOne(void);
void KtScenarioPfDevice(void);
void KtScenarioDoubleFault(void);
void KtScenarioRaiseDown(void);
void KtScenarioLowerUp(void);
void KtScenarioLowerInSection(void);

#endif /* KAMOS_KTEST_INTERNAL_H */

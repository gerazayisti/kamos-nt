/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * In-kernel test runner (ADR-0010). The scenario is chosen by the QEMU fw_cfg file
 * opt/kamos/test: absent or "all" runs the full suite; any other name runs one scenario that is
 * expected to end in a bug check (its own boot, exit value 0x12). Without a fw_cfg device
 * (real hardware) the kernel only idles.
 *
 * Result lines follow docs/testing/oracles-wave1.md section 1. This file uses the primitives of
 * tests/framework/include/ktest.h but, unlike kt_run, also prints the detail of a passing test,
 * because the timer test reports t100ns=<v> on a PASS line. It is the only file that includes
 * ktest.h (the header keeps its state in a static variable).
 */

#include "../../tests/framework/include/ktest.h"
#include "ktest_internal.h"

static char KtNote[120];

static void NoteKey(kt__buf *B, const char *Key)
{
    while (KtNote[B->len] != '\0') {
        B->len++;
    }
    if (B->len != 0) {
        kt__cat(B, " ");
    }
    kt__cat(B, Key);
    kt__cat(B, "=");
}

void KtNoteU64(const char *Key, uint64_t Value)
{
    kt__buf b = {KtNote, sizeof KtNote, 0};

    NoteKey(&b, Key);
    kt__cat_u64(&b, Value, 0);
}

void KtNoteHex(const char *Key, uint64_t Value)
{
    kt__buf b = {KtNote, sizeof KtNote, 0};

    NoteKey(&b, Key);
    kt__cat_u64(&b, Value, 1);
}

void KtNoteStr(const char *Key, const char *Value)
{
    kt__buf b = {KtNote, sizeof KtNote, 0};

    NoteKey(&b, Key);
    kt__cat(&b, Value);
}

void KtFail(const char *File, int Line, const char *Msg, int HasVals, uint64_t A, uint64_t B)
{
    kt__fail(File, Line, Msg, HasVals, A, B);
}

void KtSkip(const char *Reason)
{
    kt__st.cur_skipped = 1;
    kt__st.detail[0]   = '\0';
    {
        kt__buf b = {kt__st.detail, sizeof kt__st.detail, 0};
        kt__cat(&b, Reason);
    }
}

/* One line, one console write. */
void KtPrintLine(const char *Suite, const char *Name, const char *Result, const char *Detail)
{
    char    line[300];
    kt__buf b = {line, sizeof line, 0};

    line[0] = '\0';
    kt__cat(&b, "KTEST ");
    kt__cat(&b, Suite);
    kt__cat(&b, " ");
    kt__cat(&b, Name);
    kt__cat(&b, " ");
    kt__cat(&b, Result);
    if (Detail != NULL && Detail[0] != '\0') {
        kt__cat(&b, " ");
        kt__cat(&b, Detail);
    }
    line[b.len++] = '\n'; /* kt__cat turns newlines into spaces; the line has room for one */
    kt__put(line, b.len);
}

static unsigned RunTables(const KTX_TABLE *Tables, size_t TableCount)
{
    char    end[80];
    kt__buf e = {end, sizeof end, 0};

    kt__st.pass = kt__st.fail = kt__st.skip = 0;
    for (size_t t = 0; t < TableCount; t++) {
        for (size_t i = 0; i < Tables[t].Count; i++) {
            const KTX_CASE *c = &Tables[t].Cases[i];
            const char     *res;

            kt__st.cur_failed = kt__st.cur_skipped = 0;
            kt__st.detail[0]                       = '\0';
            KtNote[0]                              = '\0';
            c->Fn();
            res = kt__st.cur_failed ? "FAIL" : (kt__st.cur_skipped ? "SKIP" : "PASS");
            if (kt__st.cur_failed) {
                kt__st.fail++;
            } else if (kt__st.cur_skipped) {
                kt__st.skip++;
            } else {
                kt__st.pass++;
            }
            KtPrintLine(c->Suite, c->Name, res,
                        (kt__st.cur_failed || kt__st.cur_skipped) ? kt__st.detail : KtNote);
        }
    }
    end[0] = '\0';
    kt__cat(&e, "KTEST-END pass=");
    kt__cat_u64(&e, kt__st.pass, 0);
    kt__cat(&e, " fail=");
    kt__cat_u64(&e, kt__st.fail, 0);
    kt__cat(&e, " skip=");
    kt__cat_u64(&e, kt__st.skip, 0);
    end[e.len++] = '\n';
    kt__put(end, e.len);
    return kt__st.fail;
}

/* ---- Scenarios ------------------------------------------------------------------------------ */

static void NothingToDo(void)
{
}

void KtScenarioFellThrough(const char *Name)
{
    static const KTX_CASE c[1] = {{"scenario", "fell_through", NothingToDo}};
    KTX_TABLE             t    = {c, 1};

    kprintf("scenario %s: no bug check (ASSERT is compiled out in release builds)\n", Name);
    HalHaltSystem(RunTables(&t, 1) == 0 ? HAL_EXIT_TESTS_PASSED : HAL_EXIT_TESTS_FAILED);
}

static void ScenarioAssert(void)
{
    kprintf("scenario assert: failing an ASSERT on purpose\n");
    ASSERT(KiBootMagic == 0);
    KtScenarioFellThrough("assert");
}

/* Diagnostic, not a test: prints the TSC deltas between the calibration ticks, in kilocycles. */
static void ScenarioCalDump(void)
{
    for (ULONG i = 2; i < 102; i += 10) {
        char    line[200];
        kt__buf b = {line, sizeof line, 0};

        line[0] = '\0';
        kt__cat(&b, "caldump");
        for (ULONG k = i; k < i + 10; k++) {
            kt__cat(&b, " ");
            kt__cat_u64(&b, (HalpCalibrationSample(k + 1) - HalpCalibrationSample(k)) / 1000, 0);
        }
        kprintf("%s\n", line);
    }
    HalHaltSystem(HAL_EXIT_TESTS_PASSED);
}

static const struct {
    const char *Name;
    void (*Run)(void);
} Scenarios[] = {
    {"caldump", ScenarioCalDump},
    {"assert", ScenarioAssert},
    {"pf-nofixup", KtScenarioPfNoFixup},
    {"pf-offbyone", KtScenarioPfOffByOne},
    {"pf-device", KtScenarioPfDevice},
    {"df", KtScenarioDoubleFault},
    {"irql-raise-down", KtScenarioRaiseDown},
    {"irql-lower-up", KtScenarioLowerUp},
    {"irql-lower-section", KtScenarioLowerInSection},
};

_Noreturn void KtMain(void)
{
    char      sel[64];
    int       n = HalpFwCfgReadFile("opt/kamos/test", sel, sizeof sel);
    KTX_TABLE tables[3];
    unsigned  failed;

    if (n == -2) {
        kprintf("no test device: idling\n");
        for (;;) {
            __asm__ volatile("hlt");
        }
    }
    if (n > 0 && memcmp(sel, "all", 4) != 0) {
        for (size_t i = 0; i < KAMOS_ARRAY_SIZE(Scenarios); i++) {
            if (memcmp(sel, Scenarios[i].Name, strlen(Scenarios[i].Name) + 1) == 0) {
                Scenarios[i].Run();
                HalHaltSystem(HAL_EXIT_TESTS_FAILED); /* a scenario must not return */
            }
        }
        kprintf("unknown scenario '%s'\n", sel);
        HalHaltSystem(HAL_EXIT_TESTS_FAILED);
    }
    tables[0].Cases = KtBootCases;
    tables[0].Count = KtBootCaseCount;
    tables[1].Cases = KtExcCases;
    tables[1].Count = KtExcCaseCount;
    tables[2].Cases = KtIrqlCases;
    tables[2].Count = KtIrqlCaseCount;
    failed          = RunTables(tables, KAMOS_ARRAY_SIZE(tables));
    HalHaltSystem(failed == 0 ? HAL_EXIT_TESTS_PASSED : HAL_EXIT_TESTS_FAILED);
}

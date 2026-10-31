# Status: QA (tests, framework, evidence)

Owner: A10

| Feature | Status | Evidence |
|---|---|---|
| Serial log parser and judge (`tests/framework/ktest-run.py` parse, selftest) | Tested (host): 25 hand-judged fixtures | docs/testing/evidence.md 2026-10-31 |
| Runner that starts QEMU and stamps lines (`ktest-run.py qemu`) | Implemented. Used only on the timer-noise probe; not yet run against a kernel | evidence: timer noise |
| `ktest.h`, host backend | Tested (host) through `test_ktest_selftest` and the negative program | evidence 2026-10-31 |
| `ktest.h`, kernel backend | Implemented: compiles with the ADR-0003 flags, only `kt_sink` undefined. Not run in a kernel (A03's console does not exist) | evidence 2026-10-31 |
| Host harness with ASan and UBSan, canaries (`run-host-tests.sh`) | Tested (host). `make test-host` waits for A02's include line | evidence 2026-10-31 |
| Oracles for Phase 1 and 2 | Designed | docs/testing/oracles-wave1.md |
| Timer noise floor under QEMU TCG | Tested (QEMU) for the method only, 30 runs | evidence 2026-10-31 |
| Fuzzing, regression suite, threat model B1 to B6 tests | Planned | |

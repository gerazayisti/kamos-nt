# ADR-0010: Test strategy

- Status: Accepted
- Date: 2026-10-30
- Owner: A01, implemented by A10 and A02

## Decision

| Layer | Where it runs | Tooling | Status label it can grant |
|---|---|---|---|
| Unit tests of pure logic (bitmaps, page-table walkers on simulated memory, handle tables, PE parser) | Host | System compiler, ASan, UBSan | `Tested (host)` |
| Kernel tests | QEMU, headless | In-kernel test framework, parsable serial output, exit through `isa-debug-exit` | `Tested (QEMU)` |
| Fuzzing | Host | libFuzzer or AFL++ on the PE parser and syscall argument decoding | Findings only |
| Conformance | A licensed Windows 11 VM running our own test programs as black boxes | `tests/conformance/` | Reference behaviour only |
| Hardware | Real machines | Manual protocol in `docs/testing/hardware.md` | `Tested (hardware)` |

- Serial protocol: one line per result, `KTEST <suite> <name> PASS|FAIL|SKIP [detail]`,
  ending with `KTEST-END pass=<n> fail=<n> skip=<n>`.
- QEMU exit: the kernel writes `v` to port `0xF4`; QEMU returns `(v << 1) | 1`. Amended
  2026-10-31 (RFC-0002):

  | `v` | QEMU exit | Meaning |
  |---|---|---|
  | `0x10` | 33 | Tests ran, none failed |
  | `0x11` | 35 | Tests ran, at least one failed |
  | `0x12` | 37 | Bug check, including a failed `ASSERT` |

  Any other exit code, or the timeout, means the kernel did not finish. A test that expects a
  bug check (for example a deliberate `ASSERT`) states the exit code it expects, and the harness
  also requires the matching serial line, so it cannot be confused with a crash.
- Every test run has a timeout. A hang is a failure.
- A test without a clear oracle is not a test. A regression test is written before its fix.
- A10 records every acceptance criterion in `docs/testing/evidence.md`: command, date,
  commit, observed output.

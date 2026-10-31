# Coordination board

Each agent updates only its own rows. Blocked work stays blocked here; nobody works around it.

| ID | Task | Owner | Status | Blocker | Evidence |
|---|---|---|---|---|---|
| P0-01 | ADR-0001 to ADR-0012 drafted | A01 | Done (draft), awaiting A10 review | | `docs/adr/` |
| P0-02 | Interface headers in `sdk/include/kamos/` | A01 | Done (provisional) | | `make headers-check` |
| P0-03 | Host packages installed (Annex A.1) | A02 | Done | | `tools/check-toolchain.sh`: all host tools OK |
| P0-04 | x86_64-elf cross toolchain built and pinned | A02 | Done (GCC 16.2.0, binutils 2.47, multilib no-red-zone), awaiting A10 replay | | `toolchain/versions.txt`, evidence 2026-10-31 |
| P0-05 | `make toolchain-check` passes | A02 | Done, awaiting A10 replay | | evidence 2026-10-31 |
| P0-06 | `make iso` builds the placeholder ISO | A02 | Done with the cross toolchain, awaiting A10 replay | | evidence 2026-10-31 |
| P0-07 | `make test` passes (QEMU exit 33) | A02, A10 | Done with the cross toolchain, awaiting A10 replay | | evidence 2026-10-31 |
| P0-08 | Two builds give identical `kernel.elf` and ISO | A02 | Done (ISO dates pinned to SOURCE_DATE_EPOCH), awaiting A10 replay from a clean checkout | | evidence 2026-10-31 |
| P0-09 | CI workflow | A02 | Ran on GitHub (run 41193587030): 4 of 5 jobs green. Red proof done 2026-10-31 (run 41197225495): build jobs go red on a broken source, branch deleted. D14 fix merged locally (2e2bd47), not pushed | Push of main, then confirm D14 and profiles on a remote run | evidence 2026-10-31 |
| P0-09b | Pin the actions to full commit SHAs after the first remote run | A02 | Planned | P0-09 remote run | |
| P0-10 | ADR consistency review | A10 | Reviewed 2026-10-31, **not signed**: contradictions found (D01 to D05), all five now decided by A01 in RFC-0001 | A10 re-review of RFC-0001 by an independent session | `docs/reports/p0-A10.md` |
| P0-11 | Threat model skeleton, provenance audit | A10 | Done (skeleton and SPDX audit); open items D07, D08 | | `docs/security/threat-model.md`, `docs/compatibility/provenance.md` |
| D01 | IRQL in CR8 does nothing under the PIC (ADR-0009 vs ADR-0001). Blocks Wave 1 gate | A01, A03, A05 | Resolved by A01 (RFC-0001, ADR-0009), awaiting A10 re-review | | `docs/rfc/RFC-0001-phase0-contract-gaps.md` |
| D02 | `SeAccessCheck` unreachable from the Ob open path; no namespace insert. Blocks Wave 3 | A06, A01 | Resolved by A01 (RFC-0001, `ob.h`, `se.h`), awaiting A10 re-review | | `docs/rfc/RFC-0001-phase0-contract-gaps.md` |
| D03 | Missing contracts: boot info (A03), address spaces (A04), handle table lifecycle (A06, A07) | A03, A04, A06, A07 | Resolved by A01 (RFC-0001, `boot.h`, `mm.h`, `ob.h`), awaiting A10 re-review | | `docs/rfc/RFC-0001-phase0-contract-gaps.md` |
| D04 | `ke.h`: no DPC or work item, no `KeInitializeEvent`, no lock rank, no blocking lock | A05, A01 | Resolved by A01 (RFC-0001, `ke.h`, `lock-order.md`), awaiting A10 re-review | | `docs/rfc/RFC-0001-phase0-contract-gaps.md` |
| D05 | ADR-0007, ADR-0009 vs headers: NULL-returning APIs, `STATUS_CANCELLED`, `KeBugCheckEx`, wait level | A01 | Resolved by A01 (RFC-0001, ADR-0007, ADR-0009), awaiting A10 re-review | | `docs/rfc/RFC-0001-phase0-contract-gaps.md` |
| D06 | NTSTATUS values not checked against MS-ERREF; "customer bit never used" overclaim | A01, A10 | A10: 16 of 16 values equal [MS-ERREF] 2.3.1 (`provenance.md`). Customer-bit wording fixed. Left for A01: add the reserved N bit sentence (D15). Awaiting A01 to close | | `docs/compatibility/provenance.md` |
| D07 | Chromium stub citation has no revision; not replayable | A01, A10 | A10: revision recorded (chromium/src 9fd91410ef3d); the matcher accepts three shapes, so ADR-0006 "required" is too strong (D13). Awaiting A01 wording | | `docs/compatibility/provenance.md` |
| D08 | Legal review: Windows VM licence terms, SDK header redistribution | Founder | Open (Medium) | A lawyer | same |
| D09 | Referenced files missing (table.def, hardware.md, overview.md, toolchain.md, CI, matrix pages) | A01, A02, A10 | Recounted at c373958 by A10 (list in `docs/reports/p0-A10-rereview.md`); awaiting A01/A02 to create or reword | | same |
| D10 | `ASSERT` always on; `MmVirtualToPhysical` has no error value | A01, A04 | Open (Low) | | same |
| P0-12 | Phase 0 gate file | A01 | Written 2026-10-31: approved with reservations R1 to R5, **A10 has not signed** | A10 signature (re-review W1-05) | `docs/reports/gate-phase0.md` |
| W1-01 | Methodology note, A03 | A03 | Assigned | | `docs/coordination/tasks/W1-A03.md` |
| W1-02 | Methodology note, A02 | A02 | **Methodology accepted** with conditions C1 to C3 | | `docs/coordination/methodology/W1-A02-review.md` |
| W1-03 | Methodology note, A04 | A04 | Written (36ef029), not yet accepted by A01 | | `docs/coordination/tasks/W1-A04.md` |
| W1-04 | Methodology note, A10 | A10 | **Methodology accepted** with RFC-0002 decisions; contracts and harness before replay | | `docs/coordination/methodology/W1-A10-review.md` |
| W1-05 | Independent re-review of RFC-0001 and RFC-0002 and replay of unverified evidence | A10 | Done 2026-10-31 (signed with reservations R-a to R-c, defects D13 to D17). Replay of 13 rows done, see evidence | | `docs/reports/p0-A10-rereview.md`, `docs/testing/evidence.md` |
| W1-06 | Kernel build rules in the Makefile | A02 | Done, merged (`kbuild-selftest` 12/12 reviewed by A01); real-kernel use waits for A03's first source | | evidence 2026-10-31 (A02 rows) |
| W1-07 | Host unit-test harness with ASan and UBSan | A10 | Done: `tests/framework/host/run-host-tests.sh`. `make test-host` waits for A02's include line (inbox A02) | A02 include | `tests/framework/`, evidence 2026-10-31 |
| W1-08 | Oracles for the Phase 1 and 2 criteria (timer, exceptions, level masking) | A10 | Done (document): `docs/testing/oracles-wave1.md`; timer method noise measured | | `docs/testing/oracles-wave1.md` |
| W1-09 | Phase 1: boot, console, boot information | A03 | Assigned | W1-01 accepted, W1-06 | |
| W1-10 | Phase 2: GDT, IDT, exceptions, PIC, PIT, software levels | A03 | Assigned | W1-09 | |
| W1-11 | Host-side frame allocator, page tables, pool | A04 | Done on host (`Tested (host)`), awaiting A01 review; code was written before A01 accepted the note (W1-03), see its section 8 | W1-03 accepted, W1-07 | `docs/architecture/memory.md`, `docs/status/mm.md`, `docs/reports/p3-host-A04.md`, evidence 2026-10-31 (A04 rows) |
| W1-12 | Split the cache key: host tool versions out of `versions.txt` (C1) | A02 | Done, merged. Note: `check-toolchain.sh` reads `gcc=` from `versions.txt`, which stays there | | `toolchain/host-tools.txt` |
| W1-13 | `ASSERT` per build profile (D10): debug-only `ASSERT`, `VERIFY`, security checks never removed | A01 (header), A02 (`KAMOS_DEBUG` switch) | Header done 2026-10-31; `KAMOS_DEBUG` switch pending (A02, task R) | | `sdk/include/kamos/bugcheck.h` |
| W1-14 | Exit-code table: harness distinguishes 33, 35, 37; `run-test.sh` wiring | A10 (parser), A02 (wiring) | Parser done (`tests/framework/ktest-run.py`); wiring waits for A02 (inbox A02) | W1-04 | `docs/rfc/RFC-0002-wave1-test-contracts.md` |
| D12 | Findings from the A10 methodology read: `ob.h` granted access, `boot.h` wording, lock-order wording, `irql.h` owner, wait timeout sign, ADR-0002 text | A01 | Fixed in RFC-0002, awaiting A10 re-review | | `docs/rfc/RFC-0002-wave1-test-contracts.md` |
| D13 | ADR-0006 says the stub shape is "required by the Chromium sandbox"; the Chromium matcher accepts three x64 shapes | A01 | Fixed 2026-10-31 (A01, awaiting A10 check) | | `docs/compatibility/provenance.md` D07 |
| D14 | ISO not reproducible on the GitHub runner (identical locally, 65 s apart and with another TZ). Cause unknown | A02 | Fixed: BIOS-only ISO (`-d`), merged and pushed 2026-10-31; awaiting the remote run | Needs the next remote run | run 41193587030 |
| D18 | ADR-0011 `gs:[0x58]` (A10's report calls it D14) is not documented by the public `winternl.h` TEB page; clang emits it for `__declspec(thread)` | A01 | Fixed 2026-10-31 (A01, awaiting A10 check) | | `docs/compatibility/provenance.md` |
| D15 | ADR-0007 does not mention the reserved N bit (bit 28, MUST be 0 in MS-ERREF) | A01 | Fixed 2026-10-31 (A01, awaiting A10 check) | | `docs/compatibility/provenance.md` D06 |
| D16 | RFC-0002 item 8: TSC calibrated on the PIT; a busy-polling guest sees QEMU PIT time at about 0.8 of host time; calibration shape is a design risk | A03 | Open (Medium, design obligation) | | `docs/testing/oracles-wave1.md` section 5 |
| D17 | `run-test.sh` accepts only exit 33; the placeholder prints a constant `KTEST-END` with no `KTEST` line, which the parser rejects | A02 | Open (Low) | | `docs/reports/p0-A10-rereview.md` |

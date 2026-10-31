# Wave 1 gate: A10 opinion

Owner: A10. Date: 2026-11-01. Tree: `fc542ce` (main after the merge of A02, A03 and A04). Everything
below was run by A10 in a clean clone (`~/replay3`) or in scratch copies (`~/mut`), one job at a time,
on a 4 GB WSL machine, QEMU 8.2.2 with `-accel tcg`. Nothing was pushed. No Microsoft binary was used.

## 1. Replay of the unverified evidence rows

Rows 22 to 33 and 51 to 62 of `docs/testing/evidence.md` now carry A10's observation. Summary:
every script of `ci.yml` was run once locally and returned 0, with these exceptions.

| Row | Result |
|---|---|
| 27, 28, 33 | Not replayable: they are GitHub runs and `gh` is not installed here. Not verified by A10. |
| 29 | Not replayed (needs `sudo apt-get`). The negative script shows the all-platform ISO still differs (35 bytes). |
| 22, 24 | Not replayable as written: the default kernel is the real one now. Seen on the real kernel instead. |
| 51 | The tag to SHA mapping needs `gh`; only the local pin check was run (17 uses pinned). |
| 55, 56, 60, 62 | The statement "all scenarios PASS" is not reproduced: 1 of 4 first-pass cells and 1 of 16 later runs failed. See section 3. |

## 2. Mutation checks of A03's tests

Method: a scratch clone at `fc542ce`, one deliberate defect in the kernel source, `make KAMOS_DEBUG=1 iso`,
then `tests/hal/run-scenarios.sh debug qemu64 <scenario>`; the test must go red (non-zero). Script used:
`~/mut.py` (not committed, the table is the record). "all" is the full suite, plus the named scenario.

| # | Defect | Result | Test that failed |
|---|---|---|---|
| M01 | Level after an exception restored to PASSIVE instead of the interrupted level | KILLED | `pfpolicy expect_at_device_level` (levelAfter 0x0 vs 0xc), `handler_frame`, `pic spurious7` |
| M02 | Level not restored after an exception | KILLED | next fault reported as stop (`UNHANDLED_EXCEPTION`) |
| M03 | Exception handler does not raise to DEVICE_LEVEL (R-a) | **SURVIVED** | none: nothing observes the level inside the handler |
| M04 | No `cli` on `KeRaiseIrql(DEVICE_LEVEL)` (tick not masked) | KILLED | `irql if_per_level` (FlagIf 0x200 vs 0), `disabled_section`, `device_holds_tick`, `expect_at_device_level` |
| M05 | #DF not on its IST | KILLED | scenario `df` |
| M06 | Test fixup also matches `rip - 1` | SURVIVED in the kernel | the scenario `pf-offbyone` only registers `rip + 1` |
| M06b | Test fixup also matches `rip + 1` | KILLED | scenario `pf-offbyone` |
| M07 | Static fixup lookup ignores the low bit | SURVIVED in the kernel, KILLED on the host | host `test_fixup test_near_misses` (FaultRip + 1 == NULL) |
| M08 | Fixup allowed at DEVICE_LEVEL | KILLED | scenario `pf-device` and `all` |
| M09 | Fixup policy reads the level after the raise (always DEVICE) | KILLED | `all` (static fixups no longer resume) |
| M10 | Fixup used for any vector, not only #PF | **SURVIVED** | none: no test registers a FIXUP-class entry at a non-#PF fault |
| M11 | Software interrupt can nest (both guards removed) | KILLED | `dispatch depth_is_one` (SoftMaxDepth 4 vs 1), `held_until_lowered`, `held_at_device` |
| M12 | Software interrupt runs at DEVICE_LEVEL | KILLED | `dispatch held_until_lowered`, `held_at_device` |
| M13 | `HalRestoreInterrupts` never reopens IF | KILLED | `irql disabled_section`, `pfpolicy fixup_in_disabled_section` |
| M14 | #PF fetch bit decoded from bit 3 | KILLED | the `EXC-PF ... fetch=1` line in the manifest |
| M15 | Clock multiplier 2 percent high | KILLED | the host-clock ratio (O4), 10 s run |
| M16 | EOI sent for a spurious IRQ 7 | **SURVIVED** | none: with the ISR empty the test cannot tell |
| M17 | CR2 reported as 0 | KILLED | `pf not_present_read`, `not_present_write`, `null_guard` |
| M18 | Delivery loop runs once (while becomes if) | KILLED | `dispatch depth_is_one` (runs >= 3) |
| M20 | Resume path sets IF in the saved frame | KILLED | `fixup_in_disabled_section`, `expect_at_device_level` |

Totals: 20 mutations, 16 killed (15 by the kernel suite, 1 by a host test where the kernel survived), 4 survived
(M03, M06, M10, M16). Each survivor is a missing test, opened as a defect (BOARD D19, D20).

Notes on oracle quality (not mutation results):
- `dispatch depth_is_one` judges depth with `SoftMaxDepth`, a counter of the code under test. It was killed by M11
  and M18, so it works, but a stack-pointer oracle that does not trust the HAL would be stronger.
- `pic spurious7` states "no EOI wrongly clearing another line" but runs with an empty ISR, so M16 cannot show.
  A kill needs IRQ 0 in service when the spurious interrupt arrives.
- A03's `all.manifest` and the scenario manifests are hand-written, with `line`, `require`, `forbid` and a `ratio`.
  I checked them against the oracles of `oracles-wave1.md`: O3 (assert), O4 (timer), O5, O6 (exceptions, #PF bits),
  O7 (policy), O8 (levels) are all covered. I did not write duplicate manifests under `tests/framework/expected/`;
  the real ones are `tests/hal/*.manifest`.

## 3. Timer (O4) is flaky

20 runs of the full suite (4 first pass, 16 later), guest against host clock: 18 inside 0.99 to 1.01,
one at 1.0105 (debug qemu64, FAIL), one run failing without a ratio line (qemu64, cause not captured).
Range of the others 0.9918 to 1.0073. A03 reports 10 boots in 0.9961 to 1.0037. In the failing run the
guest span was 10.010 s and the host span 9.906 s, so the host saw less time than the guest. A host stall
before the start stamp would explain it; I did not prove it. I did not widen the band.
Consequence: `make test` on this machine is not deterministic. Decision for A01: accept a documented band of
about 2 percent (RFC with these numbers), or keep 1 percent and rerun once on a ratio-only failure. I propose
neither without A01's choice.

## 4. A04: mutation check and model independence

- `tests/mm/mutation-check.sh` replayed: 8 of 8 KILLED, rc 0, same killers as in the evidence row.
- `test_random` judges against a per-page model written in the test and an independent walker (`mm_dump.h`)
  built on `x86_oracle.h` (my SDM-derived PTE constructor), not on `mm_pagetable.c`. The leaf expectation is
  `x86o_pte(...)`, so the model does not share code with the implementation. The rollback property is checked
  with a frame snapshot. 20 seeds, 2500 operations each, 3 windows straddling table boundaries.
- My 10 extra mutations on `mm_pagetable.c`: 6 KILLED (NX never set, USER never set, kernel/user mismatch,
  range wrap, translate drops NX, translate write from last level); 3 SURVIVED (canonical check removed, zero
  count accepted, top-half hole accepted as kernel). All three survivors are redundant checks: a non-canonical
  address, a zero count and the hole already fail the region and wrap tests that follow, so they are equivalent
  mutants, not test gaps. One mutation (user tables without USER bit) did not compile under `-Werror`
  (unused parameter), so it says nothing.
- A04's code is only exercised on the host with a simulated machine. It is not linked into the kernel that
  boots in QEMU. `Tested (host)`, not `Tested (QEMU)`.
- `boot.h` rule R-c is implemented in `hal/bootinfo.c` and host tested (15 cases). Two mutations (different-type
  overlap tolerated, touching same-type not merged) were KILLED.

## 5. Status of what is proven

| Claim | Level | Basis |
|---|---|---|
| Framework: parser, fixtures, sanitizer canaries | Host | 25 fixtures, canaries for gcc and clang |
| Boot information parsing, R-c merge, limits | Host | `test_bootinfo` 15, 2 mutations killed |
| Fixup lookup exact match | Host | `test_fixup` 4, M07 killed |
| Frame database, page tables, pool, address space, lock order | Host | A04 suites, 8 plus 6 mutations killed |
| Boot, banner, ASSERT debug 37 / release 33 | QEMU (TCG) | `build-test` 4 cells, scenario `assert` |
| Exceptions DE, UD, BP, GP, #PF and its error bits against the SDM | QEMU (TCG) | suites exc and pf, mutations M14, M17 |
| ADR-0013 policy: no fixup, off by one, DEVICE_LEVEL, #DF on IST | QEMU (TCG) | scenarios, M05, M06b, M08 killed |
| IF per level, tick held at DEVICE_LEVEL, disabled section, dispatch depth 1 | QEMU (TCG) | suite irql and dispatch, M04, M11 to M13, M18, M20 killed |
| Clock within 1 percent of host over 10 s | QEMU (TCG), **flaky** | section 3 |
| Reproducible `kernel.elf` and ISO | Local, two builds | `reproducible.sh` debug and release |
| Handler runs at DEVICE_LEVEL (R-a) | Design only | M03 survived |
| Fixup only for #PF | Design only | M10 survived |
| NMI and #MC on their own IST, nesting of IST handlers | Design only | no scenario |
| Spurious IRQ gets no EOI (except IRQ 15 to the master) | Design only | M16 survived, ISR empty in the test |
| BAD_BOOT_INFORMATION bug check in QEMU | Design only | host test of the parser; no QEMU scenario with a bad map |
| Memory manager inside the kernel | Not started | host only |
| Everything on real hardware or with KVM | Not tested | TCG only |
| CI on GitHub | Not seen by A10 | rows 27, 28, 33 |

## 6. Residual risks

1. Timer test non-deterministic (section 3): the gate command may fail for a reason that is not a kernel defect.
2. Four test gaps (D19, D20): a regression in the handler level, in the "only #PF" rule or in the spurious EOI
   would pass today.
3. TCG only. The TSC re-measurement exists because the guest TSC changed rate between boots on this host
   (2.706 and 2.959 GHz in A03's notes, 2.754 to 2.958 GHz in my logs); no evidence about hardware.
4. `ObDuplicateHandle` access rule still not stated in `ob.h`; self-requeuing DPC not bounded (R-b).
5. The memory manager is host only.

## 7. Opinion

**Go with reservations.** The Phase 1 and 2 criteria are met on QEMU with oracles that do not share code with the
kernel, 16 of 20 deliberate defects turn the suite red, and the four survivors are missing tests, not wrong
behaviour that I observed. Conditions before the gate file is signed as "approved":
- C1. A01 decides the timer band (section 3), so that `make test` is deterministic.
- C2. A03 adds the tests for D19 (non-#PF fixup, level inside the handler) and D20 (spurious EOI with IRQ 0 in
  service); small, in `hal/ktest`. I will rerun M03, M10, M16 on the new tests.
- C3. A01 marks rows 27, 28, 33 as "CI only" in the gate file; I cannot see them.

Not a "no-go": none of the survivors is a bug I could show; each is the absence of a test.

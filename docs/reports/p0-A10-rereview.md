# Phase 0 re-review by A10: RFC-0001 and RFC-0002

Date: 2026-10-31. Reviewed commit: `990e3f0` (A10 branch after `git merge main`, which contains
RFC-0002 `e69eb6c`, the mode normalisation `50c80ce`, ADR-0013).
Scope: RFC-0001 (D01 to D05), RFC-0002 (13 items), ADR-0002, 0006, 0007, 0009, 0010, 0011, 0013,
`lock-order.md`, headers `sdk/include/kamos/*.h` as amended.

## Independence

I did not write RFC-0001, RFC-0002, the CI scripts or the Phase 0 build changes, and this session
has no memory of them. That is a statement about the session boundary: every commit in the history
has one git identity, so history cannot show it, and I am the same model family as the authors. To
compensate, each "agree" below says what text I compared, and several checks were run against
public sources fetched in this session (recorded in `docs/compatibility/provenance.md`). Nothing
here is a legal opinion, and nothing is a claim about Windows compatibility.

## Method

Read the amended text line by line against the contexts it claims, walked each changed API through
its use (open path, boot path, lock use, interrupt path), checked numbers by arithmetic, and
compared factual claims with public sources where one exists. No kernel code exists yet, so
behaviour claims are reviewed on paper and then listed as test obligations in
`docs/testing/oracles-wave1.md`; none is verified by execution.

## Verdicts on RFC-0001

| Defect | Verdict | Basis |
|---|---|---|
| D01 (levels under the PIC) | **Agree, with two reservations** | The mechanism (software level, `IF` clear at `DEVICE_LEVEL` and above, pending bits for `DISPATCH_LEVEL`) is self-consistent and needs no APIC. Its justification sentence "CR8 masks nothing under the PIC" is **not verified**: the Intel SDM was not retrievable in this session (a search returned only kernel mailing-list threads, none authoritative). The decision does not depend on it, because the software mechanism holds either way. Reservations: **R-a** nothing says at which level an exception handler (#PF, #GP) runs or that `RFLAGS` is restored from the saved frame; ADR-0013 rule 1 says a fixup is "allowed at `PASSIVE_LEVEL` only", so the level of the interrupted code must be known at fault time. **R-b** the end of a `DEVICE_LEVEL` handler runs pending software interrupts with `IF` set (`KeLowerIrql` below `DEVICE_LEVEL` sets it, ADR-0009) while still on the interrupt's stack; the stack used (RSP0 or an IST entry) and the nesting bound are not stated. Test obligation O8 case (6) in the oracles file. |
| D02 (access check on the open path) | **Agree for the open path; residual gap on the creation path** | After RFC-0002 item 4, `ObReferenceObjectByName` returns `*GrantedAccess` (`ob.h:90-94` area) and `ObInsertHandle` takes it: name, check, handle is now closed. The creation path (`ObCreateObject`, `ObInsertObject`, `ObInsertHandle`) has the creator supply `GrantedAccess` with no check; `ob.h` does not say that `ObInsertHandle` rejects bits outside the type's `ValidAccessMask`, nor that `ObDuplicateHandle` cannot grant more than the source handle holds. Both are statements A06 must add before Wave 3. |
| D03 (cross-agent contracts) | **Agree** | `boot.h` now says a map that overlaps is a bug check and that normalisation is only sorting and dropping zero-length entries (RFC-0002 item 5): the contradiction is gone. Reservation **R-c**: real firmware memory maps can contain overlapping entries; the policy "bug check" is safe but may make a real PC unbootable, which belongs in the M-HW risk list. `mm.h` address-space lifetime rules and `ObInheritHandles` read consistently. `BOOT_MODULE_NAME_MAX` is 32 and the behaviour for a longer name is not stated (not "never truncated" like the map). |
| D04 (`ke.h`, lock order) | **Agree** | DPC, work item, `KeInitializeEvent`, rank in the lock, `KMUTEX` all present and consistent with `lock-order.md`. `lock-order.md` rule 1 now states the same-rank exception (RFC-0002 item 6), so rule 1 and rule 2 no longer contradict. Timeouts are defined (item 9). Test obligations: rank assertion and level assertions (A05 Wave 2). |
| D05 (consistency) | **Agree** | Mechanical check done by reading: the 16 names of ADR-0007's initial set equal the 16 `#define` of `status.h` (and the 16 values equal the public table, D06 below); `KeBugCheckEx` is the name in ADR-0007 and `bugcheck.h`; ADR-0009's wait rule equals `ke.h`; the allocator exception is stated in ADR-0007 and `mm.h`. |

## Verdicts on RFC-0002

| # | Item | Verdict |
|---|---|---|
| 1 | Exit values 0x10/0x11/0x12 | **Agree.** Consistent between ADR-0010 and `hal.h`. The runner implements the table. `run-test.sh` still accepts only 33 and must be extended (A02, item 1 of the impact list). |
| 2 | ADR-0013 page fault policy | **Agree with a note.** The three rules are testable (oracles O7). The "exact address" match is a good rule; the table format is still undocumented, so the host test of the table (O11) cannot be written yet. |
| 3 | `KeRaiseIrql` owner | **Agree.** `irql.h:5` and the A03 brief now say the same thing. |
| 4 | `GrantedAccess` | **Agree**, see D02. |
| 5 | `boot.h` overlap | **Agree**, see D03 (R-c). |
| 6 | `lock-order.md` exception | **Agree.** |
| 7 | `IF` and `HalDisableInterrupts` | **Agree with R-a/R-b.** The invariant is checkable: `pushfq` at each level, which is how O8 observes it. A `HalDisableInterrupts` leaf section that raises no level is a good rule; a test must prove `KeLowerIrql` inside one asserts. |
| 8 | Time from the TSC calibrated on the PIT | **Agree in principle; risk found by measurement.** It removes the lost-tick problem on slow consoles. Two points. First, the TSC must exist and be usable: under QEMU `qemu64` it is; on real hardware an invariant TSC is needed (CPUID bit, not checked by the ADR), otherwise the rate changes with the power state. Second, I measured how QEMU TCG behaves with PIT time (section "Measured" of `oracles-wave1.md`): a guest that **busy-polls** the PIT counter sees PIT time run at about 0.80 of the host clock, a guest that **sleeps in `hlt` and counts interrupts** tracks the host clock to within 1 %. Calibration by busy-polling the PIT against the TSC is the first shape. If the TSC and PIT both follow QEMU's virtual clock the calibration is self-consistent and the later `hlt`-based tests are right; if they do not, `HalQueryMonotonicTime` will be about 20 % wrong under QEMU. O4 will show which, and the calibration code must be written so that it is not tuned to hide it. |
| 9 | Wait timeout sign | **Agree.** `const LONG64 *` can carry a negative value and the header says it is rejected; test obligation. |
| 10 | ADR-0002 wording | **Agree.** Text now names `readelf -d` and `nm -u`. |
| 11 | Modes | **Agree, verified**: `git ls-files -s` shows scripts 100755, others 100644 after the merge (count in the evidence file). |
| 12 | OWNERS rules | **Agree.** I rely on rule 2 to append defects to `BOARD.md`. |
| 13 | `status/build.md` wording | **Agree**; I checked it matches `entry.asm` (two fixed lines chosen by the magic). |

## New findings (not in RFC-0001 or 0002)

| ID | Severity | Owner | Finding |
|---|---|---|---|
| D12 | Medium | A01 | ADR-0006 says the stub shape is "required by the Chromium sandbox". At chromium/src `9fd91410ef3d` (and the file still exists on `main`) the matcher accepts **three** x64 shapes (plain, a Windows 8 variant with three `mov` before the prefix, and one with `test byte ptr`/`jne` before `syscall`). The plain shape is one of them, not the only one. Proposed wording in `provenance.md` (D07). Resolves D07 by correcting the claim. (D11 of `p0-A10.md` is an Info item; new IDs start at D12.) |
| D13 | Medium | A01 | ADR-0011 says the TEB TLS pointer is at `gs:[0x58]`, citing public SDK headers. The public page for `winternl.h` `TEB` documents `Reserved1[12]` before `ProcessEnvironmentBlock` and no TLS pointer at that offset (it would be `Reserved1[11]`); it also tells programs not to read the structure directly. The `NT_TIB` page URL tried returned 404. So `gs:[0x58]` is not backed by the source ADR-0011 names. Proposed: rest the row on code emitted by our own compilers (allowed by ADR-0012). I checked what clang emits (see evidence). |
| D14 | Low | A01 | ADR-0007 does not mention the reserved N bit (bit 28, "MUST be 0" in [MS-ERREF] 2.3). `KAMOS_ERROR_CODE` leaves it 0; add one sentence. |
| D15 | Medium | A03 | O4/RFC-0002 item 8: calibration shape risk above. Not a defect in the text; a design obligation. |
| D16 | Low | A02 | `tools/qemu/run-test.sh` cannot tell exit 35 or 37 from other failures; and the placeholder's `KTEST-END pass=1` with no `KTEST` line is rejected by the parser by design. A02 to print a real `KTEST` line in the placeholder or keep `run-test.sh` until A03. |
| D09 | Medium | A01, A02 | Recount at HEAD, see below. |

D09 recount at `990e3f0`. Referenced as if existing, absent: `sdk/syscalls/table.def`
(ADR-0006:34); `docs/testing/hardware.md` (ADR-0010:15); `tests/conformance/` (ADR-0010:14);
`docs/architecture/overview.md` (OWNERS.md:5, plan); `docs/development/toolchain.md` (W1-A02);
`docs/status.md` and `docs/compatibility/matrix.md` (the plan's names; the repository uses
`docs/status/` and `docs/compatibility/matrix/`); `docs/compatibility/matrix/{pe,abi,win32,chromium}.md`
(OWNERS.md). Existing now that D09 listed as missing: `.github/workflows/`, `tests/framework/`,
`docs/security/threat-model.md`. Planned by an accepted task brief (not dangling): `docs/architecture/{boot-contract,interrupts,memory}.md`,
`docs/development/toolchain.md`, `tools/debug/`, `docs/reports/p1-*.md`. `toolchain/host-tools.txt`
is referenced by the A02 methodology review and does not exist yet. Method: a scan of
backticked paths in tracked `.md .h .sh .yml .mk .py .c` files, then `test -e`; the first scan was
exact for paths with a directory part; bare filenames were checked by hand with `grep`.

## Verdict on the Phase 0 gate

The Phase 0 criterion "A10 finds no contradiction in the ADRs" is **met for design contradictions**
(D01 to D05 and the RFC-0002 items are closed or reduced to the reservations above). It is **not met
for factual claims**: D12 and D13 are statements in ADRs that the public sources do not support as
written. Both are one-line edits by A01. My signature status: **signed with reservations R-a to R-c,
D12 and D13 to be corrected**, subject to the replay in `evidence.md`, whose outcome is recorded
there. A10 does not sign anything about the quality of the placeholder test (see `status/build.md`).

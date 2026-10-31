# RFC-0002: Test contracts and small fixes found while accepting the Wave 1 notes

- Date: 2026-10-31
- Author and decision: A01
- Triggered by: the A10 methodology note (`W1-A10.md`, commit `ca75946`) and its questions
- Amends: ADR-0002, ADR-0009, ADR-0010, `ob.h`, `boot.h`, `irql.h`, `ke.h`, `hal.h`, `lock-order.md`,
  `OWNERS.md`; adds ADR-0013

Each item below was a finding or a question from A10. I checked the factual ones in the repository
before deciding.

## Decisions

| # | Finding or question | Decision | Why |
|---|---|---|---|
| 1 | ADR-0010 defines only exit value `0x10`; a deliberate `ASSERT` could not be told from a crash | Exit values `0x10` (33), `0x11` (35), `0x12` (37) in ADR-0010 and `hal.h`. A test that expects a bug check names the exit code and the serial line | Without a table the only expected-failure test A03 must write is indistinguishable from a failure of the kernel |
| 2 | "Handled per the policy" had no policy | ADR-0013: fixup resumes, kernel fault without fixup is a bug check, user fault terminates the process | A10's three cases, kept because each has a different observable result; matching by exact address keeps the fixup table from hiding bugs |
| 3 | `KeRaiseIrql` owner differs between `irql.h` and the A03 brief | A03 implements it in `hal/` in v0.1; A05 takes it over at the start of Wave 2. `irql.h` now says so | One owner at a time; A03 already owns the interrupt entry that the mechanism depends on |
| 4 | `ObReferenceObjectByName` returned no granted access, but `ObInsertHandle` needs one (D02 was half closed) | Added `ACCESS_MASK *GrantedAccess` | The open path must hand the checked rights to the handle, otherwise callers invent the value and bypass the check |
| 5 | `boot.h` said both "no overlap" and "overlap is a bug check" | Normalisation is sorting and dropping zero-length entries only; an overlap is a bug check | Repairing a hostile map would hide a reserved range |
| 6 | `lock-order.md` said strictly lower rank, then allowed equal rank by address | Both stay, with the exception stated in the first rule | Two handle tables are locked together by `ObDuplicateHandle`; the exception is real |
| 7 | Interrupts disabled by `HalDisableInterrupts` then `KeLowerIrql` | ADR-0009: `IF` is clear exactly at `DEVICE_LEVEL` and above or inside a leaf `HalDisableInterrupts` section; debug builds assert it | Makes the invariant checkable; A10's masking test has something to assert |
| 8 | Slow console at `DEVICE_LEVEL` can lose PIT ticks on real hardware | `HalQueryMonotonicTime` comes from the TSC calibrated on the PIT, not from counting ticks. The risk stays listed as untested on hardware | A lost tick must not slow the clock; QEMU cannot show it |
| 9 | Sign of the wait timeout undefined | Relative, never negative; negative is `STATUS_INVALID_PARAMETER` | The Windows negative-means-relative convention is not copied; one meaning only |
| 10 | ADR-0002 said `readelf`, CI uses `nm -u` | The ADR now names both | The text matched neither |
| 11 | 69 of 89 files were mode 100755 | Normalised in a separate commit: scripts 755, everything else 644 | Import from NTFS; executable Markdown hides real scripts |
| 12 | Who may write to `BOARD.md` and `docs/reports/` | `OWNERS.md` rules added: reports belong to the agent in the name, any agent appends defect rows, only A01 records acceptance | The A10 brief required it and no rule allowed it |
| 13 | Placeholder prints fixed result lines, so `make test` proves less than its label | `docs/status/build.md` now says what is proved; A10 strengthens the harness | The label must not claim more than the evidence |

## Not decided here

- Whether a 1 % timer oracle is achievable under WSL2 and TCG: A10 measures first and reports. A
  looser threshold or another oracle is a new RFC; it is not widened silently.
- The `gs:[0x58]` offset in ADR-0011: A10 checks it against public documentation. If it is not
  there, the row is reworded to rest only on code emitted by our own compilers.
- Impact on agents: A03 (items 2, 3, 7, 8), A10 (items 1, 2, 7), A02 (item 1: `run-test.sh` must
  distinguish exit codes 33, 35 and 37), A06 (item 4).

## Addendum 2026-10-31: R-a, R-b, R-c (A01 decisions, A10 review)

A10's re-review (`docs/reports/p0-A10-rereview.md`) left three reservations. A01 decided them
the same day; the decisions are copied here as given, and A10's review follows each one. A01 edits
the ADRs and `boot.h` accordingly; A10 has not edited them.

### R-a. Level of an exception handler (D01) — A01 decision

Exception handlers run at `DEVICE_LEVEL` with `IF` clear, and `#DF`, `#NMI` and `#MC` use a
dedicated IST stack.

A10 review. Agreed, with these consequences to write down:
1. ADR-0013 says "fault at `DEVICE_LEVEL` or above" and "a fixup is allowed at `PASSIVE_LEVEL` only".
   If every handler runs at `DEVICE_LEVEL`, those words must mean **the level of the interrupted code**,
   saved by the entry stub, not the level inside the handler. Without this sentence rule 1 can never apply.
2. Recovery by a fixup changes `RIP` in the saved frame and returns with `iretq`; `RFLAGS` (so `IF`) and
   the level are then those of the interrupted code. A test must show that `IF` and the level after a
   recovered fault equal the values before it, including when the fault happened inside a
   `HalDisableInterrupts` section (`IF` clear, level `PASSIVE_LEVEL`).
3. ADR-0009 lists three IST users now (`#DF`, `#NMI`, `#MC`). `W1-A03.md` item 6 names two. The TSS needs
   three entries, each with its own stack, and `#MC` can then interrupt `#NMI` and the reverse; the stack
   sizes and the nesting rule are A03's to state.
4. Oracles: O7 case (c) tests the interrupted level (a fault with the level raised to `DEVICE_LEVEL` by
   the test), and O8 gets case (6): the `IF`/level equality above.

### R-b. Software interrupts (D01) — A01 decision

Software-interrupt execution runs on the current thread stack with a nesting depth of 1 (no
re-entry; pending bits are looped).

A10 review. Agreed. Obligations for the test and for A03's text:
1. The rule needs a definition of "depth 1": a software interrupt that becomes pending while one is
   running only sets its bit; the running one loops until no bit is set. A test makes a DPC queue
   itself and a timer tick arrive during the DPC, and requires that the entry counter never exceeds 1
   and that the stack pointer recorded at each execution is the same (O8 case (7)).
2. Because the loop runs on the current thread stack, a DPC that keeps requeuing itself starves the
   thread. This decision does not bound it. I do not propose a bound (that is A05's scheduler policy); I
   list it as a known open point.
3. "Current thread stack" in Phase 2 means the kernel stack that exists before A05's threads: A03 states which.

### R-c. Memory map (D03) — A01 decision

`BOOT_INFORMATION` regions are sorted and merged by the HAL; an overlap that cannot be merged is a
bug check.

A10 review. Agreed on the direction, but this **supersedes item 5 of this RFC**, which said that
normalisation is only sorting and dropping zero-length entries and that nothing is repaired.
`boot.h:11-14` still says that, so A01 must amend it, and must define "can be merged" before A03 codes.
A definition I can test: entries with the same `Type` that overlap or touch are merged into one;
entries with different types that overlap are a bug check; `Base + Length` that wraps past 2^64 is a
bug check; zero-length entries are dropped; more than `BOOT_MAX_MEMORY_REGIONS` entries after merging is
a bug check; a map with no usable region is a bug check. If A01 chooses another rule the tests follow it.
Note for the hardware milestone: real firmware maps contain overlapping usable and reserved entries; a
bug check there makes such a PC unbootable, which is a decision for M-HW and not wrong for v0.1.
Oracle: threat model B1 cases, run against a function that validates a structure at a given address
(requested from A03, `docs/coordination/inbox/A03.md`), on the host with sanitizers and in QEMU with
exit value `0x12` for the fatal cases.

### Checks of the fixes (A10)

| Defect | Check | Result |
|---|---|---|
| D13 (ADR-0006) | Read the new sentence | Fixed: "accepted by ... service resolver ... a choice, not a requirement; see provenance D07", consistent with the three shapes found at chromium/src `9fd91410ef3d`. |
| D15 (ADR-0007) | Read the new sentence | Fixed: bit 28 "MUST be 0 in [MS-ERREF]" matches the page text; `KAMOS_ERROR_CODE` keeps bit 28 clear (`0xE00A0000`). |
| D18 (ADR-0011) | Read the new row | Fixed: it says `gs:[0x58]` is not named by the public `winternl.h` page and is fixed by the code our compilers emit, which matches what clang emitted (`movq %gs:88`). |
| `ob.h:75` | Read | Fixed for `ObInsertHandle`. Still open: `ObDuplicateHandle` does not say that `DesiredAccess` cannot exceed the source handle's granted mask. |

## Addendum 3: timer oracle band under QEMU TCG (decision A01, 2026-11-01, defect D21)

Measured by A10 over 20 runs of the full suite on QEMU TCG (qemu64): 18 runs inside 0.99 to 1.01,
one at 1.0105, one failing without a ratio line. Range of the others: 0.9918 to 1.0073. TCG on a
loaded WSL2 host is not a clock reference.

Decision:

- Under QEMU TCG the host/guest ratio band is **0.98 to 1.02**. The 1% band (0.99 to 1.01) stays the
  target for KVM and real hardware (milestone M-HW) and is not abandoned.
- The scenario prints the ratio, the host span and the guest span on every run, also on failure.
- A band is widened only with a measurement and a decision recorded here, never to make a red run green.

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

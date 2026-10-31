# Phase 0 gate file

Prepared by A01 on 2026-10-31. Signed by A10: **no** (see "Reservations").

## Founder decision

Approved with reservations, on the founder's instruction given in the session of 2026-10-31
("approuve la suite et donne les tâches aux agents"). The founder delegated the recording of the
approval to the A01 session. No other person approved it, and A10 has not signed.

## Deliverables by agent

| Agent | Deliverable | State |
|---|---|---|
| A01 | ADR-0001 to ADR-0012, headers in `sdk/include/kamos/` (now including `boot.h`), roadmap, status files, compatibility matrix, clean-room and licence policy, RFC-0001 | Done, provisional contracts |
| A02 | Cross toolchain GCC 16.2.0 and binutils 2.47, `make` targets, reproducible ISO, QEMU test harness, CI workflow and scripts | Done. The CI workflow has not run on GitHub yet |
| A10 | ADR and header review, threat model skeleton, provenance register, evidence log | Done for the review. The re-review of RFC-0001 and an independent replay are still to do |

## Acceptance criteria of Phase 0

| Criterion | Result | Evidence |
|---|---|---|
| `make toolchain-check` and `make all` succeed, an ISO is produced | Met | `docs/testing/evidence.md`, 2026-10-31 |
| Ten ADRs written, A10 finds no contradiction | **Not met** until the re-review of RFC-0001 (12 ADRs exist) | `docs/reports/p0-A10.md` |
| Every frozen interface has a named owner | Met (owner line in each header, `OWNERS.md`) | `sdk/include/kamos/*.h` |
| No entry marked Tested without proof | Met | `docs/status/`, `docs/testing/evidence.md` |
| Founder approves the gate file | Approved with reservations | this file |

## Reservations

1. **R1.** D01 to D05 were decided by A01 in RFC-0001 and have not been re-reviewed by an
   independent A10 session. A03 implements ADR-0009 as amended; if the re-review changes it, A03
   adapts.
2. **R2.** The CI workflow is validated locally (YAML parses, every script passes) but has not
   run on GitHub. Pushing needs the founder's explicit go-ahead (ADR-0004). The first run builds
   the cross toolchain and may take one to two hours.
3. **R3.** D06 to D10 stay open. D08 (legal review of the Windows VM licence terms and of SDK
   header redistribution) is the founder's, with a lawyer.
4. **R4.** The Phase 0 evidence was produced by the authoring sessions. A10 replays it
   independently during Wave 1.
5. **R5.** The plan names ten agents; the repository also has A09b and A11. They are not active in
   Wave 1.

## Wave 1 (phases 1 and 2)

Active agents, at most four (plan, decision 11): A03 (critical path), A02, A10, A04 (host work
only). A01 orchestrates. Tasks: `docs/coordination/tasks/W1-<ID>.md`.

Not started: A05, A06, A07, A08, A09, A09b, A11. A09's PE parser is the first to start after
Wave 1 because it is host-only and independent. It is held back to respect the four-agent limit
and because A04's host work shortens Wave 2, which is on the critical path (A03, A04, A05, A07).

Gate to leave Wave 1 (plan 5.1 and roadmap criteria 1 to 3): the kernel boots under QEMU on
`qemu64` and `max` and exits with code 33; exceptions #DE, #UD, #GP and #PF are decoded and
logged and a deliberate page fault is handled; the timer is within 1 % over 10 s; level masking
is validated. A10 signs.

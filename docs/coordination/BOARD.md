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
| P0-09 | CI workflow (SPDX, toolchain cache, build and boot on qemu64 and max, reproducibility, broken-source self-test) | A02 | Implemented, scripts pass locally, **not run on GitHub** | Push needs the founder's go-ahead | evidence 2026-10-31 |
| P0-09b | Pin the actions to full commit SHAs after the first remote run | A02 | Planned | P0-09 remote run | |
| P0-10 | ADR consistency review | A10 | Reviewed 2026-10-31, **not signed**: contradictions found (D01 to D05), all five now decided by A01 in RFC-0001 | A10 re-review of RFC-0001 by an independent session | `docs/reports/p0-A10.md` |
| P0-11 | Threat model skeleton, provenance audit | A10 | Done (skeleton and SPDX audit); open items D07, D08 | | `docs/security/threat-model.md`, `docs/compatibility/provenance.md` |
| D01 | IRQL in CR8 does nothing under the PIC (ADR-0009 vs ADR-0001). Blocks Wave 1 gate | A01, A03, A05 | Resolved by A01 (RFC-0001, ADR-0009), awaiting A10 re-review | | `docs/rfc/RFC-0001-phase0-contract-gaps.md` |
| D02 | `SeAccessCheck` unreachable from the Ob open path; no namespace insert. Blocks Wave 3 | A06, A01 | Resolved by A01 (RFC-0001, `ob.h`, `se.h`), awaiting A10 re-review | | `docs/rfc/RFC-0001-phase0-contract-gaps.md` |
| D03 | Missing contracts: boot info (A03), address spaces (A04), handle table lifecycle (A06, A07) | A03, A04, A06, A07 | Resolved by A01 (RFC-0001, `boot.h`, `mm.h`, `ob.h`), awaiting A10 re-review | | `docs/rfc/RFC-0001-phase0-contract-gaps.md` |
| D04 | `ke.h`: no DPC or work item, no `KeInitializeEvent`, no lock rank, no blocking lock | A05, A01 | Resolved by A01 (RFC-0001, `ke.h`, `lock-order.md`), awaiting A10 re-review | | `docs/rfc/RFC-0001-phase0-contract-gaps.md` |
| D05 | ADR-0007, ADR-0009 vs headers: NULL-returning APIs, `STATUS_CANCELLED`, `KeBugCheckEx`, wait level | A01 | Resolved by A01 (RFC-0001, ADR-0007, ADR-0009), awaiting A10 re-review | | `docs/rfc/RFC-0001-phase0-contract-gaps.md` |
| D06 | NTSTATUS values not checked against MS-ERREF; "customer bit never used" overclaim | A01, A10 | Partly: customer-bit wording fixed (ADR-0007). Values still unchecked against MS-ERREF | | same |
| D07 | Chromium stub citation has no revision; not replayable | A01, A10 | Open (Medium) | | same |
| D08 | Legal review: Windows VM licence terms, SDK header redistribution | Founder | Open (Medium) | A lawyer | same |
| D09 | Referenced files missing (table.def, hardware.md, overview.md, toolchain.md, CI, matrix pages) | A01, A02, A10 | Open (Medium) | | same |
| D10 | `ASSERT` always on; `MmVirtualToPhysical` has no error value | A01, A04 | Open (Low) | | same |
| P0-12 | Phase 0 gate file | A01 | Written 2026-10-31: approved with reservations R1 to R5, **A10 has not signed** | A10 signature (re-review W1-05) | `docs/reports/gate-phase0.md` |
| W1-01 | Methodology note, A03 | A03 | Assigned | | `docs/coordination/tasks/W1-A03.md` |
| W1-02 | Methodology note, A02 | A02 | Assigned | | `docs/coordination/tasks/W1-A02.md` |
| W1-03 | Methodology note, A04 | A04 | Assigned | | `docs/coordination/tasks/W1-A04.md` |
| W1-04 | Methodology note, A10 | A10 | Assigned | | `docs/coordination/tasks/W1-A10.md` |
| W1-05 | Independent re-review of RFC-0001 and replay of unverified evidence | A10 | Assigned | W1-04 accepted | |
| W1-06 | Kernel build rules in the Makefile | A02 | Assigned | W1-02 accepted. Blocks A03's first build | |
| W1-07 | Host unit-test harness with ASan and UBSan | A10 | Assigned | W1-04 accepted. Blocks A04's tests | |
| W1-08 | Oracles for the Phase 1 and 2 criteria (timer, exceptions, level masking) | A10 | Assigned | W1-04 accepted. Blocks A03's tests | |
| W1-09 | Phase 1: boot, console, boot information | A03 | Assigned | W1-01 accepted, W1-06 | |
| W1-10 | Phase 2: GDT, IDT, exceptions, PIC, PIT, software levels | A03 | Assigned | W1-09 | |
| W1-11 | Host-side frame allocator, page tables, pool | A04 | Assigned | W1-03 accepted, W1-07 | |

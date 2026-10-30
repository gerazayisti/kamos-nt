# Coordination board

Each agent updates only its own rows. Blocked work stays blocked here; nobody works around it.

| ID | Task | Owner | Status | Blocker | Evidence |
|---|---|---|---|---|---|
| P0-01 | ADR-0001 to ADR-0012 drafted | A01 | Done (draft), awaiting A10 review | | `docs/adr/` |
| P0-02 | Interface headers in `sdk/include/kamos/` | A01 | Done (provisional) | | `make headers-check` |
| P0-03 | Host packages installed (Annex A.1) | A02 | Done | | `tools/check-toolchain.sh`: all host tools OK |
| P0-04 | x86_64-elf cross toolchain built and pinned | A02 | In progress | | `toolchain/versions.txt` |
| P0-05 | `make toolchain-check` passes | A02 | Planned | P0-03, P0-04 | |
| P0-06 | `make iso` builds the placeholder ISO | A02 | Done with host binutils; redo with cross toolchain | | evidence 2026-10-30 |
| P0-07 | `make test` passes (QEMU exit 33) | A02, A10 | Done with host binutils, awaiting A10 replay | | evidence 2026-10-30 |
| P0-08 | Two builds give identical `kernel.elf` and ISO | A02 | Partial: kernel.elf identical, ISO not | xorriso dates | evidence 2026-10-30 |
| P0-09 | CI workflow (debug, release, qemu64, max) | A02 | Planned | Remote host not chosen | |
| P0-10 | ADR consistency review | A10 | Planned | P0-01 | |
| P0-11 | Threat model skeleton, provenance audit | A10 | Planned | | |
| P0-12 | Phase 0 gate file | A01 | Planned | P0-05 to P0-10 | |

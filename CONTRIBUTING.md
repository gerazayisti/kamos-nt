# Contributing to Kamos-NT

## Truth rules

- Nothing is declared done unless it is implemented and tested.
- A test passes only if it ran and its output was observed. If you could not run it, say so
  and give the command.
- A failing implementation is never replaced by a stub announced as a success.
- Every feature carries a status in `docs/status/<subsystem>.md`: `Planned`, `Designed`,
  `Implemented`, `Built`, `Tested (host)`, `Tested (QEMU)`, `Tested (hardware)`, `Stubbed`,
  `Partially supported`, `Unsupported`, `Blocked`.

## Clean room

Read [ADR-0012](docs/adr/0012-compatibility-and-clean-room.md) before your first change.
Never use leaked Windows source, decompiled Microsoft binaries, or code from projects under
an incompatible license. When in doubt about a source, stop and open an RFC.

## Code

- Kernel: C11 freestanding, NASM (Intel syntax) for entry points and stubs.
- Naming: [ADR-0008](docs/adr/0008-naming.md). Status codes: [ADR-0007](docs/adr/0007-status-codes.md).
- Every file starts with `SPDX-License-Identifier: Apache-2.0` and the copyright line.
- Every public header documents preconditions, allowed execution levels (ADR-0009) and
  error codes. Every structure documents its owner, lifetime and protecting lock.
- Code, comments, commits and repository documents are in English.

## Branches and reviews

- One git worktree per agent; branches `agent/<id>/p<N>-<topic>`.
- Commits follow Conventional Commits (`feat(mm): ...`, `fix(ke): ...`, `docs(adr): ...`).
- A merge needs green CI and a peer review. Changes to `sdk/include/` also need A01.
- You never merge your own branch, and you only edit directories your role owns
  (`docs/coordination/OWNERS.md`). For anything else, write to the owner's inbox in
  `docs/coordination/inbox/`.

# Report: Phase 3 on the host (A04)

Branch `agent/a04/p3-mm-host`. Status labels: `Tested (host)` for the frame allocator, page tables,
address spaces and pool; `Designed` for kernel stacks and the user-copy fixup. Nothing `Tested (QEMU)`.

## What exists

`ntoskrnl/mm/` (frame database, page tables, address spaces, pool, public wrappers) behind the
`MM_HW_OPS` seam, `arch/x86_64/mm/pte.h`, and `tests/mm/host/` with 5 test programs and a
simulator. Design: `docs/architecture/memory.md`. Status: `docs/status/mm.md`.

## Observed results

- Harness: `tests/framework/host/run-host-tests.sh`, gcc 13.3.0 and clang 18.1.3, ASan and UBSan:
  frames 10/10, pagetable 9/9, pool 9/9, random 1/1 (20 seeds, 2500 operations each), space 6/6,
  `test-host: PASS`.
- Mutation check (`tests/mm/mutation-check.sh`): 8 of 8 defects killed.
- cppcheck 0, clang-format 0, spdx-check PASS on my files.

## Problems found and fixed during the work

- Another agent used the same staging directory name and its files ended up in my first commit
  (252c87d); I reset it and recommitted only my paths (cbde46f).
- cppcheck does not treat `KeBugCheckEx` as noreturn; `return;` added after stops.
- The first mutation run reported one mutation as a build error (unused variable) that the script
  counted as a kill; the script now flags `BUILD-ERROR`, the mutation was corrected and re-run: killed.

## Not done, not proven

- Wave 1 gate order: the task file says no code before A01 accepts the methodology note. I was told to
  continue; code commits precede acceptance (note section 8). A01 may reject the design.
- Nothing ran on hardware or QEMU: accessed and dirty bits, `invlpg`, SMP and TLB shootdown, SMAP, the
  real boot map and the direct-map bootstrap (A03 assumptions A-1 to A-5, `inbox/A03.md`).
- Kernel stacks and the user-copy fixup are design only.
- Small slab pages are not returned; the free-list link is not hardened.
- Requests to A01 (stop code, D10 error value, statuses) are in `inbox/A01.md`; `make test-host` still
  needs A02's include line (not mine to edit).
- Evidence rows are in the unverified column for A10 to replay.

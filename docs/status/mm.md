# Status: Memory

Owner: A04

Labels: `Designed`, `Tested (host)`. Nothing is `Tested (QEMU)`: no MM code has run in the kernel yet.
Design: `docs/architecture/memory.md`. Report: `docs/reports/p3-host-A04.md`.

| Feature | Status | Evidence |
|---|---|---|
| Boot map classification, boot allocator, frame database (`mm_frame.c`) | Tested (host) | `tests/mm/host/test_frames.c`, 10 cases, gcc and clang with ASan and UBSan |
| PTE layout, map, unmap, protect, translate, rules, rollback (`mm_pagetable.c`) | Tested (host) | `test_pagetable.c` (9 cases), `test_random.c` (20 seeds against a model) |
| Address spaces, kernel half sharing, destroy, public wrappers, user-range check (`mm_space.c`, `mm_api.c`) | Tested (host) | `test_space.c`, 6 cases |
| Kernel pool, tags, debug fill, corruption stops (`mm_pool.c`) | Tested (host) | `test_pool.c`, 9 cases, 12 random seeds |
| Lock order 30/60/70 | Tested (host) | lock-order checker in every test: 0 violations, depth 0, no lock held across the page provider |
| Mutation check (8 deliberate defects) | Tested (host) | `tests/mm/mutation-check.sh` |
| Kernel stacks with guard pages | Designed | `docs/architecture/memory.md` section 7 |
| `MmCopyFromUser`, `MmCopyToUser` and the fault fixup | Designed | `memory.md` section 6; only the range check is host-tested. Needs A03's page-fault handler |
| Large pages, demand paging, sections, copy-on-write, paged pool paging | Not started | out of scope for v0.1 |
| SMP, TLB shootdown | Not started | single processor in v0.1 |
| Behaviour on hardware or under QEMU (accessed/dirty bits, `invlpg`, real boot map, direct map bootstrap) | Not tested | waits for A03 (assumptions A-1 to A-5 in `inbox/A03.md`) |

Known limits: small slab pages are not returned to the frame database; the free-list link in a freed
block is not hardened.

<!-- SPDX-License-Identifier: Apache-2.0 -->
# Wave 1 gate (decision A01, 2026-11-01)

**Decision: approved with reservations.** Wave 1 delivers the boot path, the exception and
interrupt-level machinery, the host-side memory manager and the CI. Wave 2 may start on the
points marked "QEMU" or "host"; the points marked "design" are not claimed.

Independent opinion: A10, `docs/reports/w1-gate-A10.md` (GO with reservations). Checks run by A01
on the merged tree and the GitHub run 41249667288 (16 of 16 jobs green) back the table below.

| Area | State | Where it is proven |
|---|---|---|
| Reproducible build, CI, pinned actions, negative tests | Tested | GitHub run, A10 replay (debug and release identical) |
| Multiboot2 boot, serial console, boot information, R-c merge rule | Tested | QEMU (TCG), host tests with ASan and UBSan |
| Exceptions, #PF bits, ADR-0013 policy, #DF on IST, IF per level, tick held at DEVICE_LEVEL | Tested | QEMU scenarios; 16 of 20 A10 mutations killed |
| Software interrupt depth 1 | Tested, observation to be made independent | `depth_is_one` (D20) |
| Handler level DEVICE_LEVEL (R-a), fixup only for #PF, spurious IRQ7 | Design only | A10 mutations M03, M10, M16 survive (D19, D20): A03 task |
| Timer to host clock | Tested under TCG only, band 0.98 to 1.02 (RFC-0002 addendum 3, D21) | 1% band awaits KVM or hardware (M-HW) |
| Frame allocator, page tables, address spaces, pool | Tested on the host with a simulated machine | A04 mutations 8 of 8, A10 mutations 6 of 9 plus 3 equivalent; NOT linked to a booting kernel |
| Real hardware, KVM, SMP, TLB shootdown, SMAP copy routines | Not tested | M-HW and later waves |

Evidence rows that only GitHub can show (runs, the tag to SHA mapping) are "CI only": A10 could not
replay them locally and does not claim to.

## Reservations carried to Wave 2

1. A03: tests for M03, M10, M16 and an independent depth observation, with A10's mutations rerun.
2. The memory manager is not wired to the kernel: A04 and A03 integrate it at the start of Wave 2.
3. Timer: re-measure on KVM or hardware before closing the 1% criterion.
4. Open defects D08 (legal review, founder), D19 to D21.
5. `ObDuplicateHandle` access rule is now written in `ob.h`; A10 to confirm.
6. A DPC that re-queues itself starves the thread; bounding it is a policy for A05.

A10 signs "approved" when conditions C1 (decided above), C2 (A03 tests) and C3 (this file) are met.

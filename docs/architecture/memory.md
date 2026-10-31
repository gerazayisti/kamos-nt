# Memory manager (MM)

Owner: A04. Status: frame allocator, page tables, address spaces and pool are `Tested (host)`;
kernel stacks and the user-copy fixup are `Designed`. Nothing here is `Tested (QEMU)`.
Sources: `ntoskrnl/mm/`, `arch/x86_64/mm/pte.h`. Tests: `tests/mm/host/`. Contract: `sdk/include/kamos/mm.h`
and ADR-0005 (address-space layout).

## 1. Layers and the hardware seam

All code in `ntoskrnl/mm/` reaches the machine only through `MM_HW_OPS` (`mm_hw.h`):
`PhysicalToPointer`, `InvalidatePage`, `GetCurrentRoot`/`SetCurrentRoot`, `AcquireLock`/`ReleaseLock`
(with a rank), `GetCurrentIrql`, and an optional `NotifyFrame` hook (used by the host tests for
poisoning). The host tests supply a simulated physical memory and a lock-order checker; A03's kernel
supplies `mov cr3`, `invlpg`, the direct map and the real locks. Every `Mi*` function takes an explicit
`MM_CONTEXT`; the public `Mm*` wrappers (`mm_api.c`) use one global context set once at boot.

## 2. Physical memory

**Boot map.** `BOOT_INFORMATION` regions are sorted and merged by the HAL. MM re-checks sortedness
and non-overlap and stops with `KAMOS_STOP_BAD_BOOT_INFORMATION` (reason `0x4D01...`) on an overlap
that cannot be merged. Region edges: a usable region shrinks inward to whole pages; every other kind
grows outward. When kinds overlap the order is Bad > Reserved > Acpi > Free.

**Reserved at boot** (never handed out): the first MiB, boot modules, the framebuffer, the page holding
the RSDP, the kernel image and the loader's structure (these two come from A03 as extra reservations,
assumption A-1).

**Boot allocator.** Before the frame database exists, pages are taken from the highest usable range
that fits. It places the database itself, then closes (`MiFrameDbInit`).

**Frame database.** One 8-byte `MM_PFN` per frame (`State`, `Flags`, `RefCount`) plus a free bitmap
with a next-fit hint. States: Nonexistent, Free, Active, PageTable, Acpi, Reserved, Bad. Flags: BOOT,
DATABASE. `MiAllocateFrame` (optionally zeroed), `MiReferenceFrame`, `MiDereferenceFrame` (the caller
states the expected state). Double free, wrong state and a bad address stop the system
(`KAMOS_STOP_ASSERTION_FAILED`, parameter 1 = `0x4D4D0000 | reason`) after releasing the lock.
`MiFrameDbVerify` recounts everything and is used by the tests.

## 3. Page tables and address spaces

Layout of entries: `arch/x86_64/mm/pte.h`, checked in the tests against `x86_oracle.h` (A10, Intel SDM).
Leaves are written as `phys | P | [RW] | [US] | [XD]` and nothing else. Intermediate entries are
`table | P | RW | [US in the user half]`. Accessed and dirty bits are set by hardware and preserved by
`Protect`.

**Rules** (all enforced before the first write, every violation is a status, not a stop):
canonical address, page alignment, user range `0x10000..0x7FFFFFFEFFFF` (null guard and top guard),
kernel mappings only inside the ADR-0005 mappable regions (direct map, pool, stacks, device, image),
physical address below the CPU limit, `READ` always present, no `WRITE|EXECUTE`, no unknown bits,
the `USER` flag must match the address half, user mappings only of frames in state Active (never a
page table, a free frame or kernel memory), count between 1 and 2^36.

**Map, unmap, protect** are two-phase (validate every page, then write) with exact rollback if a table
allocation fails: the result is byte-identical to the state before the call. Statuses: collision on
a mapped page `STATUS_OBJECT_NAME_COLLISION`; unmap or protect of an unmapped page
`STATUS_INVALID_PARAMETER`; a large page in the way `STATUS_NOT_IMPLEMENTED`; table allocation
`STATUS_NO_MEMORY`. A corrupt entry (unexpected bits, a table that is not a PageTable frame) stops with
`0x4D4D0000 | MM_STOP_TABLE_CORRUPT`.

**Invariant I:** no table below the PML4 is empty, except the kernel-half PDPTs. Unmap frees tables
that became empty; rollback does the same.

**Kernel half.** At kernel-space initialisation each PML4 entry 256..511 receives its PDPT, and these
entries never change afterwards. A new address space copies the 256 kernel entries with no lock. So
two spaces always agree on the kernel half and no lazy fault-in is needed.

**Translate** handles 4 KiB, 2 MiB and 1 GiB pages and returns the effective rights: write and user
are the AND over the levels, execute is the negation of the OR of the XD bits.

**TLB.** `invlpg` is issued when the address is in the kernel half or the root is the current one. One
processor only in v0.1; no shootdown (see section 8).

## 4. Pool

Size classes 64, 128, 256, 512 and 1024 bytes. Each block has a 32-byte header (magic, tag, size, type)
and an 8-byte trailer canary derived from the block address, so the largest small payload is 984
bytes. A slab page keeps its own header in the first block. Larger requests go to the page provider in
whole pages, with the same header and trailer. Free lists are separate per `POOL_TYPE`. Tags are
accounted in 128 slots plus an `Other` slot (allocations, frees, bytes in use, peak).

Refused with `NULL`: zero size, size above 2^40, a tag that is not four printable characters, an
unknown type. `MiPoolFree(NULL)` is a no-op. In debug builds allocated memory is filled with `0xCD`,
freed memory with `0xDD`, and a freed block is checked for writes when it is handed out again.
Corruption stops with `KAMOS_STOP_POOL_CORRUPTION`, parameter 1 =
`MM_POOL_STOP_*` (bad pointer, double free, bad header, wrong tag, overrun, write after free,
accounting, live blocks at release). The pool lock is never held across a call to the provider.

Not done: small slab pages are never returned to the frame database; the free-list link inside a
freed block is not hardened (an attacker with a write-after-free primitive in release builds can still
steer the next allocation).

## 5. Locks (`docs/architecture/lock-order.md`)

| Rank | Lock | Kind | Level | Protects |
|---|---|---|---|---|
| 30 | address space (one per `ADDRESS_SPACE`, one for the kernel space) | mutex | PASSIVE | the user half of that space's tables; the kernel half is protected by the kernel-space lock |
| 60 | pool | spinlock | up to DISPATCH | free lists, tag table, counters |
| 70 | frame database | spinlock | up to DISPATCH | `Pfn[]`, bitmap, counters |

A map in the kernel half takes the kernel-space lock (30) and may then take 70 for table frames; the
pool takes 60 and releases it before asking for pages, which then takes 70 alone. The host tests
record each acquisition (`g_locks`) and prove no violation, depth zero at exit, and that the provider
is never called with a lock held. Security checks are not `ASSERT`s: they are active in every profile.

## 6. User copy and fault fixup (design, `Designed`, needs A03)

`MmCopyFromUser(dst, userSrc, n)` and `MmCopyToUser` do:

1. `MiIsUserRange(src, n)`: no wrap, entirely in `0x10000..0x7FFFFFFEFFFF`. If not,
   `STATUS_ACCESS_VIOLATION`, nothing touched. This part is host-tested (`test_user_range_check`).
2. Run at PASSIVE_LEVEL with no spinlock held (a fault can need a page-in later).
3. Copy with one routine whose faulting instructions are labelled. Each label pair `[start, end)` and a
   continuation address go into a table in the section `.kfixup` (entries of three 64-bit words).
4. A03's page-fault handler: if the fault came from kernel mode, `CR2` is in the user range and
   `RIP` is inside a table entry, it sets `RIP` to that entry's continuation address and returns. The
   routine then returns `STATUS_ACCESS_VIOLATION`. A fault with `RIP` outside every entry, or with `CR2`
   outside the user range, is a kernel bug and bugchecks. No other kernel code is allowed to fault on a
   user address.
5. With SMAP enabled the routine brackets the copy with `stac` and `clac`; both instructions are inside
   the table range so that the handler clears AC on fixup.
6. The kernel never trusts a pointer read from user memory twice (copy first, then validate the copy).

Assumption A-3 (inbox A03): the handler and the table lookup are A03's; the table format and the
rules above are mine.

## 7. Kernel stacks (design only)

`MmCreateKernelStack(Size)` maps `Size` bytes of Active frames in the stack region (ADR-0005) and
leaves one unmapped page below the lowest one (the guard). It returns the top. A fault in the guard
is a kernel stack overflow: the handler reports it on the double-fault stack (separate IST entry) and
bugchecks `KAMOS_STOP_KERNEL_STACK_OVERFLOW` if A01 defines it, otherwise
`KAMOS_STOP_ASSERTION_FAILED`. `MmDeleteKernelStack` unmaps, frees the frames and the stack's page
tables (invariant I). Gaps between stacks are never mapped, so two stacks never touch. Not implemented
yet, because it needs the thread structure from A05.

## 8. What the tests prove, and what they do not

Host tests prove: the entry layout against the SDM oracle, every rule, rollback after a failure at
every allocation point, the model-based random sequences (map, unmap, protect, translate across
2 MiB, 512 GiB and 1 GiB boundaries), frame conservation back to the initial snapshot, pool
no-overlap with fault injection, all corruption stops, lock order. `tests/mm/mutation-check.sh` breaks
eight rules on purpose and requires a test to fail.

They do not prove: behaviour on real hardware (accessed and dirty bits, TLB, `invlpg`, PCID),
SMP races and shootdown, the boot path with the real memory map, SMAP/SMEP interaction, performance.
Those are open and stay `Designed` or untested until the QEMU phases.

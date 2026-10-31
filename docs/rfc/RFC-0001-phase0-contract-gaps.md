# RFC-0001: Close the Phase 0 contract gaps (D01 to D05)

- Date: 2026-10-31
- Author and decision: A01 (decided in the founder's absence, on his instruction to decide and justify)
- Triggered by: A10 review, `docs/reports/p0-A10.md`
- Amends: ADR-0007, ADR-0009, `lock-order.md`, headers `ke.h`, `ob.h`, `se.h`, `mm.h`, `io.h`; adds `boot.h`
- State: Accepted, awaiting A10 re-review. The headers were still provisional, so this is an
  edit now rather than a frozen-interface change later.

## D01. How execution levels work under the PIC (ADR-0009)

Decision: software level variable, `DEVICE_LEVEL` and above run with `IF` clear, `DISPATCH_LEVEL`
is a pure software level driven by pending bits. CR8 stays unused until the APIC milestone.

Why this one:
- It is correct on the hardware v0.1 targets. CR8 only masks interrupts delivered through the
  local APIC, so writing it with the PIC gives the code a level that excludes nothing.
- It keeps the Phase 2 critical path short. Using the APIC from the start would put ACPI MADT
  parsing and APIC bring-up into A03's Phase 2, and the plan puts both after v0.1 (M-SMP).
- It needs only `cli`, `sti` and a bitmap, and every part can be tested under QEMU.

Rejected: masking PIC lines per level (an I/O write on every raise, about a microsecond each,
for no gain since all device lines share one level); enabling the APIC now (see above).

Limit: single processor. M-SMP revisits this through an RFC.

## D02. Reaching the access check from the open path

Decision (`ob.h`, `se.h`):
- `ObCreateObject` takes the security descriptor, not a name. `ObInsertObject` publishes the
  object under a path, and the namespace holds its own reference.
- `ObReferenceObjectByName` takes the desired access. Ob maps generic rights with the type's
  `GenericMapping`, then calls `SeAccessCheck` with `SeGetCurrentToken()`.
- `SeGetCurrentToken()` returns the running thread's token. The stub returns a static system
  token until Phase 7.

Why:
- The signatures no longer change when Phase 7 makes the check real. That removes the circular
  dependency the plan warned about (plan 1.3, point 4), instead of only naming it.
- The token is implicit, not a parameter. A system call always acts as its thread, and an
  explicit token parameter is how a caller ends up checking someone else's rights by mistake.
  Impersonation, if needed later, gets its own function.
- Creating and publishing are separate steps so that a failure after creation never leaves a
  half-built object visible by name.

## D03. Contracts between agents

- `boot.h` (A03). The loader's data is copied into fixed arrays (128 regions, 8 modules)
  during Phase 1. Why copy: the loader's memory is ordinary RAM the frame allocator could reuse,
  so keeping pointers into it ties a lifetime to a structure nobody owns. Why fixed arrays: no
  allocator exists yet. Why bug check on overflow: a silently truncated map could hide a reserved
  range, and the system would later hand it out as free memory.
- Address spaces (`mm.h`, A04). An explicit `ADDRESS_SPACE` object, with `MmMapPagesInSpace`.
  A07 needs to build a new process image without switching to it, and a bare CR3 value would let
  callers mix kernel and process spaces. Frames stay owned by whoever mapped them, so destroying
  a space cannot free a frame twice.
- Handle table lifecycle (`ob.h`, A06). Create, destroy and inherit sit in Ob because the table
  is Ob's type; A07 calls them. Inherited handles keep their values, which is what the plan's
  "héritage de la table de handles" and PE programs expect.

## D04. Kernel core gaps (`ke.h`)

- DPC and work item. Without them an interrupt handler cannot finish an I/O request
  (`IoCompleteRequest` is for DISPATCH_LEVEL at most) and object deletion cannot leave
  DISPATCH_LEVEL. Both are structures the caller embeds, because the charter forbids
  allocating in an interrupt handler.
- `KEVENT` and `KMUTEX` have public layouts and `KeInitializeEvent` exists. An opaque type
  nobody can create or embed is not usable.
- Lock rank is stored in the lock (8 bytes). It is what lets debug builds check
  `lock-order.md`, instead of promising to.
- `KMUTEX` for ranks 10 to 30. Those locks are held across work that can block, so spinlocks
  would be wrong for them. Not recursive, so a self-deadlock is caught by an assertion.
- Console lock moves to DEVICE_LEVEL and the panic path bypasses it. A lock at HIGH_LEVEL
  cannot exist (spinlocks raise to DISPATCH or DEVICE only).

## D05. Consistency

ADR-0007 now states the allocator exception, lists `STATUS_CANCELLED`, and names
`KeBugCheckEx`. ADR-0009 allows a zero-timeout wait up to DISPATCH_LEVEL, as `ke.h` did.

## Impact per agent

| Agent | Impact |
|---|---|
| A03 | Writes `boot.h` support in Phase 1; implements the software level mechanism in Phase 2; console lock at DEVICE_LEVEL |
| A04 | Address space API; frame ownership rule; may now rely on `HalGetBootInformation` |
| A05 | DPC, work item worker thread, mutex, events, lock rank assertion |
| A06 | Handle table lifecycle, `ObInsertObject`, access check in the open path |
| A07 | Uses `ObCreateHandleTable`, `ObInheritHandles`, `MmCreateAddressSpace`, `MmMapPagesInSpace` |
| A08 | ISR completes requests through a DPC |
| A09 | None |
| A10 | Re-reviews D01 to D05; tests for the lock order and level assertions |

## Migration

No code uses the old signatures; the repository has no kernel C source yet. Checked with
`make headers-check`, one translation unit including every header, and a caller using the new
API, all with the ADR-0003 flags and `-Werror`.

## Not decided here

D06 to D10 stay open (NTSTATUS values against the public specification, Chromium citation,
legal review, missing files, `ASSERT` semantics and the unmapped-address result of
`MmVirtualToPhysical`). The legal review (D08) is the founder's.

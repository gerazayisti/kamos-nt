# ADR-0013: Page fault policy

- Status: Accepted (provisional until the Wave 1 gate)
- Date: 2026-10-31
- Owner: A01, implemented by A03, with A04 and A07

## Context

Phase 2 requires that "a deliberate page fault is handled according to the policy". No policy
existed. A10 asked who decides and proposed three cases.

## Decision

The `#PF` handler decodes the error code and CR2, then applies the first rule that matches:

1. **Registered fixup.** If the faulting instruction address is in the fault-fixup table (the user
   copy routines `MmCopyFromUser` and `MmCopyToUser`, and nothing else), execution resumes at the
   fixup address and the copy returns `STATUS_ACCESS_VIOLATION`. Allowed at `PASSIVE_LEVEL` only.
2. **Fault at `DEVICE_LEVEL` or above**, or any kernel-mode fault without a fixup: bug check
   `KAMOS_STOP_UNHANDLED_EXCEPTION` with the decoded fault (address, present, write, user,
   reserved bit, instruction fetch, RIP). There is no recovery and no demand paging in v0.1.
3. **User-mode fault** (from Phase 6): the process is terminated with `STATUS_ACCESS_VIOLATION`;
   the kernel does not stop.

Faults in the fixup table are matched by exact address, never by range, so a bug elsewhere in the
copy routine cannot be hidden.

## Consequences

Wave 1 tests: a fault inside a registered test fixup resumes and is logged; a fault without a
fixup ends in a bug check with exit value `0x12` and the decoded line (ADR-0010), which the
harness expects explicitly.

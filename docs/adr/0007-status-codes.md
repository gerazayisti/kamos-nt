# ADR-0007: Status type and error codes

- Status: Accepted (provisional until the Wave 1 gate)
- Date: 2026-10-30
- Owner: A01

## Decision

- Every kernel function that can fail returns `NTSTATUS`, a signed 32-bit integer defined in
  `sdk/include/kamos/status.h`.
- Bit layout and numeric values follow the publicly documented NTSTATUS format
  (severity in bits 31–30, customer bit 29, reserved N bit 28 (MUST be 0 in [MS-ERREF]; a status with bit 28 set is invalid), facility in bits 27–16, code in bits 15–0), so
  that PE programs see the values they expect (ADR-0011). Values are taken one by one from
  public documentation, never from decompiled binaries.
- `NT_SUCCESS(s)` is `((NTSTATUS)(s) >= 0)`.
- Kamos-NT specific codes set the customer bit (`0x20000000`), which the public format
  reserves for customer-defined codes, with facility `0x0A0` (`FACILITY_KAMOS` in `status.h`).
- A recoverable error never triggers a panic. `KeBugCheckEx` is reserved for broken kernel
  invariants and carries a Kamos-NT stop code from `sdk/include/kamos/bugcheck.h`.

- One exception to the `NTSTATUS` rule: functions whose only failure is "no memory" return a
  null pointer (`MmAllocatePool`, `MmCreateKernelStack`). Everything else, including
  allocators that can fail for several reasons, returns `NTSTATUS`. Amended 2026-10-31
  (RFC-0001).

## Initial set

`STATUS_SUCCESS`, `STATUS_UNSUCCESSFUL`, `STATUS_NOT_IMPLEMENTED`,
`STATUS_INVALID_PARAMETER`, `STATUS_NO_MEMORY`, `STATUS_ACCESS_DENIED`,
`STATUS_INVALID_HANDLE`, `STATUS_ACCESS_VIOLATION`, `STATUS_OBJECT_NAME_NOT_FOUND`,
`STATUS_OBJECT_NAME_COLLISION`, `STATUS_BUFFER_TOO_SMALL`, `STATUS_TIMEOUT`,
`STATUS_PENDING`, `STATUS_INVALID_IMAGE_FORMAT`, `STATUS_INSUFFICIENT_RESOURCES`, `STATUS_CANCELLED`.

# ADR-0005: Virtual address space layout

- Status: Accepted (provisional until the Wave 2 gate)
- Date: 2026-10-30
- Owner: A01 and A04

## Decision

4-level paging, 48-bit canonical addresses. 5-level paging is out of scope until an RFC.

| Range | Size | Use | Owner |
|---|---|---|---|
| `0x0000000000000000`–`0x000000000000FFFF` | 64 KiB | Never mapped (null guard) | A04 |
| `0x0000000000010000`–`0x00007FFFFFFEFFFF` | ~128 TiB | User space, per process | A07 |
| `0x00007FFFFFFF0000`–`0x00007FFFFFFFFFFF` | 64 KiB | Never mapped (top guard) | A04 |
| `0xFFFF800000000000`–`0xFFFF87FFFFFFFFFF` | 8 TiB | Direct map of physical memory | A04 |
| `0xFFFF880000000000`–`0xFFFF8FFFFFFFFFFF` | 8 TiB | Reserved | A01 |
| `0xFFFFA00000000000`–`0xFFFFA0FFFFFFFFFF` | 1 TiB | Kernel pool | A04 |
| `0xFFFFB00000000000`–`0xFFFFB00FFFFFFFFF` | 64 GiB | Kernel stacks, each with a guard page | A04, A05 |
| `0xFFFFC00000000000`–`0xFFFFC0FFFFFFFFFF` | 1 TiB | Device mappings (MMIO, framebuffer) | A04, A08 |
| `0xFFFFFFFF80000000`–`0xFFFFFFFFFFFFFFFF` | 2 GiB | Kernel image (ADR-0002) | A03 |

Rules:

- The kernel half (PML4 entries 256–511) is shared by every address space.
- W^X everywhere in the kernel. `EFER.NXE` on. SMEP and SMAP enabled when CPUID reports
  them; user memory is touched only through `MmCopyFromUser`/`MmCopyToUser` with fault
  recovery.
- User-space ranges that PE images rely on (default image base `0x140000000`, TEB/PEB
  placement) are set by ADR-0011 within the user range above.

## Consequences

Changing any boundary is an RFC, because pool, stack and direct-map arithmetic depend on it.

# Threat model (skeleton)

Owner: A10. Status: Designed (skeleton). Nothing below is implemented or tested; every row is a
risk to be addressed by the owner named, with a test that A10 will write.

Method: one table per trust boundary. Columns are the asset, the attacker, the threat, the
planned mitigation and the phase where it must exist. A mitigation is not claimed until it has
an entry in `docs/testing/evidence.md`.

## B1. Boot loader to kernel (Phase 1, owner A03)

| Threat | Mitigation planned | Phase |
|---|---|---|
| Malformed or absent Multiboot2 information (bad tag sizes, overlapping or huge memory map entries) | Check magic `0x36D76289`, bound every tag by the total size, reject with `KAMOS_STOP_BAD_BOOT_INFORMATION` | 1 |
| Boot module (initrd) overlapping kernel or the memory map | Reserve and range-check modules before the frame allocator starts | 1, 3 |
| Boot media tampering | Out of scope for v0.1. No secure boot. State this limit in the README | later |

## B2. User mode to kernel (Phase 6, owners A07 and A04)

| Threat | Mitigation planned | Phase |
|---|---|---|
| Kernel pointer or unmapped pointer passed to a system call | `MmCopyFromUser` and `MmCopyToUser` with fault recovery; no direct dereference | 3, 6 |
| Double fetch of syscall arguments | Copy arguments once into kernel memory (ADR-0006) | 6 |
| Non-canonical `RCX` on `sysretq` | Return with `iretq` when `RCX` is not canonical (ADR-0006) | 6 |
| User code reads or writes kernel memory | Kernel half only mapped supervisor, SMEP and SMAP when CPUID reports them (ADR-0005) | 3 |
| Stale FPU or SIMD state leaking between threads | Save and restore per thread, clear on first use | 6 |
| Forged or reused system call number | Dispatch table bounds check, upper 32 bits of `rax` must be zero | 6 |

## B3. Handles and objects (Phases 5 and 7, owner A06)

| Threat | Mitigation planned | Phase |
|---|---|---|
| Forged, closed or stale handle | Index plus generation, validated on every lookup (ob.h) | 5 |
| Use after free of an object | Reference counting, deletion exactly once, deferral out of DISPATCH_LEVEL (needs D04 work items) | 5 |
| Access without permission | `SeAccessCheck` on the open path (needs D02); stubbed until Phase 7, so no security claim before then | 7 |
| Handle table exhaustion by one process | Per-process quota | 5, 7 |

## B4. PE images (Phase 10, owner A09)

| Threat | Mitigation planned | Phase |
|---|---|---|
| Header fields that overflow offset arithmetic, overlapping sections, out-of-range directories | Checked arithmetic on every offset, reject overlaps and overflows | 10 |
| Relocation or import entries pointing outside the image | Bound each entry, reject unresolved imports | 10 |
| Parser crash on hostile input | Host fuzzing with ASan and UBSan (A10), parser without dynamic allocation | 10 |

## B5. I/O requests and drivers (Phase 8, owner A08)

| Threat | Mitigation planned | Phase |
|---|---|---|
| Double completion, or completion racing cancellation | One atomic state per request, debug bug check on the second completion (io.h) | 8 |
| Unvalidated user buffer in a device control | Buffers copied or probed through the Mm primitives | 8 |
| Malicious or corrupt file system metadata | Bounds and checksum checks, defined corruption policy | 9 |

## B6. Supply chain and build (owner A02)

| Threat | Mitigation planned | Phase |
|---|---|---|
| Tampered toolchain sources | SHA-256 pins in `toolchain/versions.txt`, checked by `build-cross.sh`; GPG verification done once when pinning | 0 |
| Non-reproducible artifacts hiding a change | Replay from clean clones compares hashes (done for `kernel.elf` and ISO on 2026-10-31) | 0 |
| Third-party code of unknown origin | Provenance register, SPDX header audit before each gate | all |
| CI with no review of workflow changes | `.github/workflows/` owned by A02, reviewed by A01 | 0 |

## Out of scope for v0.1

Secure boot, side-channel mitigations, multi-user isolation (no security enforcement before Phase 7),
network attacks (no network stack), physical attacks.

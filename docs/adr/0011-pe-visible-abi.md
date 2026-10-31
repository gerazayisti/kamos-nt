# ADR-0011: ABI contracts visible to PE programs

- Status: Accepted (provisional until the Wave 5 gate)
- Date: 2026-10-30
- Owner: A01, with A09 and A11

## Context

Compiled PE code reads some structures directly instead of calling an API. Those layouts are
publicly documented and must match exactly; everything behind an API stays a Kamos-NT design.

## Decision

| Contract | Kamos-NT rule | Public source |
|---|---|---|
| User calling convention | Microsoft x64: `rcx`, `rdx`, `r8`, `r9`, 32-byte shadow space, 16-byte stack alignment | Microsoft Learn, "x64 calling convention" |
| Exceptions | `.pdata`/`.xdata` unwind tables, `RtlVirtualUnwind`, `RtlLookupFunctionEntry` | Microsoft Learn, "x64 exception handling" |
| TEB and PEB | Only the fields compiled code reads directly, at their documented offsets: `NT_TIB`, self pointer at `gs:[0x30]`, TLS pointer at `gs:[0x58]` (not named by the public `winternl.h` TEB page; it is the offset our own x64 compiler targets emit for `__declspec(thread)`, so it is fixed by the code we generate, not by a Microsoft document), stack limits, `TlsSlots`, PEB `Ldr` and `ProcessParameters` | `winnt.h`, `winternl.h` from public SDK headers, code emitted by our own compilers |
| TLS | TLS callbacks, `__declspec(thread)`, `TlsAlloc` | PE/COFF specification, `.tls` section |
| Error codes | `NTSTATUS` and Win32 error values identical; last error stored in the TEB | `ntstatus.h`, `winerror.h` |
| Handles | Opaque, multiples of 4, index plus generation, never a pointer | Microsoft Learn, kernel objects |
| API sets | `api-ms-win-*` and `ext-ms-*` names resolved by the loader to Kamos DLLs | Microsoft Learn, "Windows API sets" |
| Paths | `\??\C:\` namespace, case-insensitive, case-preserving | Microsoft Learn, "Naming files, paths, and namespaces" |
| `ntdll` stubs | ADR-0006 shape, Kamos service numbers | Chromium sandbox requirement |

Any new row is an RFC that cites a public source.

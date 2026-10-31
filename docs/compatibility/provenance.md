# Provenance register

Owner: A10. Every piece of third-party code, data or specification-derived table is listed
here before it is merged.

| Item | Path in repository | Origin | Version | License | Reviewed by | Date |
|---|---|---|---|---|---|---|
| Apache License 2.0 text | `LICENSE` | https://www.apache.org/licenses/LICENSE-2.0.txt | 2.0 | Apache-2.0 | A01 | 2026-10-30 |
| NTSTATUS values in `status.h` | `sdk/include/kamos/status.h` | [MS-ERREF] section 2.3.1 NTSTATUS values, https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55 ; bit layout: section 2.3, .../87fba13e-bf06-450e-83b1-9241dc81e781 (page updated_at 2024-10-30, git_commit_id 817392bb) | Retrieved 2026-10-31 | Facts, no code copied | A01 (2026-10-30); A10 comparison of the 16 values 2026-10-31, see D06 below | 2026-10-31 |
| libgcc no-red-zone multilib recipe | `toolchain/build-cross.sh` | OSDev wiki, "Libgcc without red zone" | | Build recipe, no code imported | A02 | 2026-10-30 |
| Syscall stub byte pattern `4C 8B D1 B8 <n> 0F 05 C3` | `docs/adr/0006-syscall-abi.md` | Chromium `sandbox/win/src/service_resolver_64.cc`, read as a reference, no code copied. Revision read: chromium/src `9fd91410ef3d77f015dc6843d7ab51bae03eaf33` (tree snapshot of 2019-10-10); the file also exists on `refs/heads/main` on 2026-10-31 with the same matcher names | Revision above | BSD-style licence per the file header ("Copyright 2012 The Chromium Authors"), BSD-3-Clause per the Chromium LICENSE file (not read) | A10, 2026-10-31, see D07 below | 2026-10-31 |
| Tools used, not imported: GCC 16.2.0, binutils 2.47, GRUB 2.12, QEMU 8.2.2, xorriso 1.5.6 | none (build host only) | See `toolchain/versions.txt` and `tools/image/GRUB-SOURCE.md` | as listed | GPL family, used as tools; GRUB shipped on the ISO with its licence and source offer (ADR-0001) | A10 | 2026-10-31 |

SPDX audit, 2026-10-31, commit `dde257e`: all 19 tracked source, header, script and build files
carry `SPDX-License-Identifier`. Markdown files are not checked. Method: `git ls-files` filtered on
`.c .h .S .asm .ld .sh .mk` and `Makefile`, then `grep`.

## D06. NTSTATUS values (A10, 2026-10-31)

Compared: the 16 `#define` values of `status.h` with the 16 rows of the public table [MS-ERREF]
2.3.1 (URL in the register above). Result: **16 of 16 equal** (SUCCESS 0, TIMEOUT 0x102, PENDING
0x103, UNSUCCESSFUL 0xC0000001, NOT_IMPLEMENTED 0xC0000002, ACCESS_VIOLATION 0xC0000005,
INVALID_HANDLE 0xC0000008, INVALID_PARAMETER 0xC000000D, NO_MEMORY 0xC0000017, ACCESS_DENIED
0xC0000022, BUFFER_TOO_SMALL 0xC0000023, OBJECT_NAME_NOT_FOUND 0xC0000034, OBJECT_NAME_COLLISION
0xC0000035, INVALID_IMAGE_FORMAT 0xC000007B, INSUFFICIENT_RESOURCES 0xC000009A, CANCELLED
0xC0000120). Limits: the page text was read through the fetch tool's summariser, not saved
byte for byte (the page is 255 362 characters; only the first 100 000 were read, the 16 names were all
found there); the values in `status.h` were compared by reading, not by a script. Layout: the page
gives Sev 2 bits, C 1 bit ("set for customer-defined values"), N 1 bit ("Reserved. MUST be set
to 0"), Facility 12 bits, Code 16 bits. `KAMOS_ERROR_CODE` gives `0xE00A0000 | code`: Sev 3, C 1,
N 0, facility `0x0A0`, consistent. ADR-0007 does not mention the N bit; a sentence "bit 28 is
reserved and stays 0" should be added by A01. `FACILITY_KAMOS 0x0A0` collides with nothing the
page was asked about (not checked against the facility table).

## D07. Chromium stub pattern (A10, 2026-10-31)

Read: `sandbox/win/src/service_resolver_64.cc` at `9fd91410ef3d77f015dc6843d7ab51bae03eaf33`.
Findings: constant `kMmovR10EcxMovEax = 0xB8D18B4C` (bytes `4C 8B D1 B8`), `kSyscall = 0x050F`,
`kRetNp = 0xC3`; struct `ServiceEntry` and matcher `IsService()` check the `4C 8B D1 B8` prefix,
`0F 05` and `C3` and ignore the service id. Two more x64 shapes are accepted: `ServiceEntryW8`
(three `mov` instructions before the prefix; syscall and ret not checked) and
`ServiceEntryWithInt2E` (a `test byte ptr [...]` / `jne` before the `syscall`). So **ADR-0006's
"required by the Chromium sandbox" is too strong**: the Chromium matcher accepts three shapes, and
the plain shape is one of them. No line numbers: the fetch tool gave none. The file still exists
on `main` (2026-10-31). Proposed wording for A01: "ntdll stubs use the plain shape
`4C 8B D1 B8 <n> 0F 05 C3`, which the Chromium sandbox's `IsService()` matcher recognises
(chromium/src `9fd91410`, `sandbox/win/src/service_resolver_64.cc`); Chromium also accepts two
other shapes, so the shape is a compatibility choice, not a requirement." The pattern is a fact
about what Chromium expects of the target, read in open-source code; nothing is copied. Whether a
byte pattern that Chromium derived from Microsoft's `ntdll` is acceptable under ADR-0012's ban on
"structures recovered by disassembling Microsoft binaries" is a policy question for A01 and the
founder; the pattern is also what `syscall` calling needs logically (`rcx` is clobbered, so the
first argument moves to `r10`).

## ADR-0011 `gs:[0x58]` and related offsets (A10, 2026-10-31)

Source read: Microsoft Learn, "TEB structure (winternl.h)", https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-teb
(page date 2024-05-29). It documents `Reserved1[12]`, `ProcessEnvironmentBlock`,
`Reserved2[399]`, `Reserved3[1952]`, `TlsSlots[64]`, `Reserved4[8]`, `Reserved5[26]`,
`ReservedForOle`, `Reserved6[4]`, `TlsExpansionSlots`, with the warning that the structure may
change and must not be accessed directly. Consequences: (1) **`gs:[0x58]` is not documented there**:
it would be `Reserved1[11]` (12 pointers precede the PEB pointer, so the PEB pointer is at 0x60),
and the page never names a TLS pointer at that offset. (2) The `TlsSlots` offset can be computed
from the public structure (12*8 + 8 + 399*8 + 1952 = 0x1480) but the page tells programs not to
read it directly. (3) `PEB.Ldr` and `ProcessParameters` were not checked on the PEB page. (4) The
`NT_TIB` page returned HTTP 404 at the URL tried, so `gs:[0x30]` (Self) was not verified from a
public page in this session; it follows from `NT_TIB` in `winnt.h` if the member order is
ExceptionList, StackBase, StackLimit, SubSystemTib, FiberData, ArbitraryUserPointer, Self
(7th pointer, offset 0x30), which I know from memory only. Recommendation to A01: reword the
ADR-0011 TEB row so that `gs:[0x58]` rests on "code emitted by our own compilers" (ADR-0012 allows
it), checked by compiling a `__declspec(thread)` access with clang for `x86_64-pc-windows-msvc`
and reading the emitted instruction, and drop `winternl.h` as its source.
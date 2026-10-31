# Provenance register

Owner: A10. Every piece of third-party code, data or specification-derived table is listed
here before it is merged.

| Item | Path in repository | Origin | Version | License | Reviewed by | Date |
|---|---|---|---|---|---|---|
| Apache License 2.0 text | `LICENSE` | https://www.apache.org/licenses/LICENSE-2.0.txt | 2.0 | Apache-2.0 | A01 | 2026-10-30 |
| NTSTATUS values in `status.h` | `sdk/include/kamos/status.h` | [MS-ERREF] 2.3, public specification | | Facts, no code copied | A01 | 2026-10-30 |
| libgcc no-red-zone multilib recipe | `toolchain/build-cross.sh` | OSDev wiki, "Libgcc without red zone" | | Build recipe, no code imported | A02 | 2026-10-30 |
| Syscall stub byte pattern `4C 8B D1 B8 <n> 0F 05 C3` | `docs/adr/0006-syscall-abi.md` | Chromium `sandbox/win/src/service_resolver_64.cc`, read as a reference, no code copied | **Not recorded (open, defect D07)** | Chromium is BSD-3-Clause, to confirm | A10 (open) | 2026-10-31 |
| Tools used, not imported: GCC 16.2.0, binutils 2.47, GRUB 2.12, QEMU 8.2.2, xorriso 1.5.6 | none (build host only) | See `toolchain/versions.txt` and `tools/image/GRUB-SOURCE.md` | as listed | GPL family, used as tools; GRUB shipped on the ISO with its licence and source offer (ADR-0001) | A10 | 2026-10-31 |

SPDX audit, 2026-10-31, commit `dde257e`: all 19 tracked source, header, script and build files
carry `SPDX-License-Identifier`. Markdown files are not checked. Method: `git ls-files` filtered on
`.c .h .S .asm .ld .sh .mk` and `Makefile`, then `grep`.

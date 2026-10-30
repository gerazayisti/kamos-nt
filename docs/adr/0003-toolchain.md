# ADR-0003: Toolchain

- Status: Accepted (provisional until the Phase 0 gate)
- Date: 2026-10-30
- Owner: A01, implemented by A02

## Decision

| Use | Tool | Notes |
|---|---|---|
| Kernel C | `x86_64-elf-gcc`, built from source | Exact versions in `toolchain/versions.txt` |
| Kernel link | `x86_64-elf-ld` (binutils, same build) | Linker script per ADR-0002 |
| Kernel assembly | NASM, Intel syntax | Entry points, context switch, interrupt stubs |
| libgcc | Built with `-mno-red-zone` (multilib `no-red-zone`) | Only if the kernel needs it; Phase 1 decides |
| PE user code (DLLs, test programs) | clang + lld, target `x86_64-pc-windows-msvc`; MinGW-w64 GCC as second compiler | No proprietary Microsoft tools |
| Host unit tests | System `gcc`/`clang` with ASan and UBSan | Pure-logic modules only |
| ISO | `grub-mkrescue` + `xorriso` | ADR-0001 |
| Emulator, debugger | QEMU `qemu-system-x86_64`, GDB | Profiles in `tools/qemu/` |

Development host: Windows 11 with WSL2 Ubuntu 24.04. The repository lives on the WSL ext4
file system (`~/src/kamos-nt`), never under `/mnt/c`, for build speed and correct file modes.

Kernel compile flags (starting point, A02 finalises them):

```
-std=c11 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-red-zone
-mcmodel=kernel -mgeneral-regs-only -fno-omit-frame-pointer -Wall -Wextra -Werror
-ffile-prefix-map=$(SRCDIR)=.
```

Reproducibility: `SOURCE_DATE_EPOCH` set from the last commit, no `__DATE__`/`__TIME__`.

## Consequences

`make toolchain-check` fails until the cross-compiler is built; this is intended. Clang with
`--target=x86_64-elf` is the documented fallback if the GCC build is blocked.

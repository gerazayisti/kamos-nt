# ADR-0002: Kernel image format

- Status: Accepted (provisional until the Wave 1 gate)
- Date: 2026-10-30
- Owner: A01, implemented by A02 and A03

## Decision

- The kernel is a static, non-relocatable ELF64 executable, `build/kernel.elf`, `ET_EXEC`,
  machine `EM_X86_64`.
- Load address (physical): 1 MiB (`0x100000`). Link address of 64-bit code and data:
  `0xFFFFFFFF80000000 + physical` (top 2 GiB, required by `-mcmodel=kernel`).
- The Multiboot2 header and the 32-bit trampoline sit in a `.boot` section linked at its
  physical address, so it runs before paging is enabled.
- Sections are page-aligned (4 KiB) so ADR-0005's W^X policy can map them separately:
  `.text` (RX), `.rodata` (R), `.data` and `.bss` (RW, NX).
- Symbols exported by the linker script: `__kernel_start`, `__kernel_end`, `__bss_start`,
  `__bss_end`, `__text_start`, `__text_end`, `__rodata_start`, `__rodata_end`.
- No dynamic sections, no `PT_INTERP`, no undefined symbols (checked by `readelf` in CI).
- Boot modules: one ustar archive, `initrd.tar`, carries the first user program (ELF64 static
  or flat binary) until the PE loader exists (Phase 10).
- Debug information stays in `kernel.elf`. A stripped copy is not produced before v0.1.

## Consequences

The linker script `arch/x86_64/boot/kernel.ld` is owned by A03. A02 owns the rules that
call it. A change to the link address is an RFC because ADR-0005 depends on it.

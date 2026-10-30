# ADR-0001: Boot protocol

- Status: Accepted (provisional until the Wave 1 gate)
- Date: 2026-10-30
- Owner: A01, implemented by A03

## Context

The kernel needs a loader that hands it a memory map, boot modules (the initrd) and a
framebuffer description. Two candidates were considered:

| Option | Entry state | License of the loader | UEFI path |
|---|---|---|---|
| GRUB2 + Multiboot2 | 32-bit protected mode, paging off | GPLv3 (shipped next to the kernel on the ISO) | GRUB EFI, same Multiboot2 contract |
| Limine | 64-bit long mode, higher-half already mapped | BSD-2-Clause | Native |

## Decision

v0.1 boots with **GRUB2 + Multiboot2**, BIOS firmware, under QEMU. The founder confirmed
this choice on 2026-10-30.

- The kernel carries a Multiboot2 header (magic `0xE85250D6`) in its first 32 KiB.
- The entry point checks `EAX == 0x36D76289`, keeps `EBX` (physical address of the boot
  information structure), builds initial page tables, enables PAE, `EFER.LME`, `EFER.NXE`
  and paging, then far-jumps to 64-bit code. That trampoline lives in `arch/x86_64/boot/`.
- Boot modules: `module2 /boot/initrd.tar` (ustar), see ADR-0002.
- Limine stays a documented option. Switching is an RFC against this ADR.
- UEFI boot is the M-UEFI milestone (after v0.2), through GRUB EFI with the same contract.

## Consequences

- A03 writes and tests a 32-bit to 64-bit trampoline in Phase 1.
- GRUB is GPLv3. It is an aggregate on the ISO, not linked with the kernel. The ISO ships
  GRUB's license and a written source offer (`tools/image/GRUB-SOURCE.md`); see ADR-0004.
- `grub-file --is-x86-multiboot2 build/kernel.elf` is a build check (A02).

/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Phase 0 placeholder link script (owner A02). The real script is ADR-0002's, owned by A03. */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 1M;
    .multiboot2 : ALIGN(8) { KEEP(*(.multiboot2)) }
    .text   : ALIGN(4K) { *(.text*) }
    .rodata : ALIGN(4K) { *(.rodata*) }
    /DISCARD/ : { *(.note*) *(.comment) }
}

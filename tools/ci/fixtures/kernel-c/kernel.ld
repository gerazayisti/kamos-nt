/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Link script of the A02 build fixture (not the kernel; the kernel's is A03's kernel.ld). */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(fixture_entry)

KERNEL_VMA = 0xFFFFFFFF80000000;

SECTIONS
{
    . = 1M;
    .boot : { KEEP(*(.multiboot2)) *(.boot.text) *(.boot.data) }
    .boot.bss (NOLOAD) : ALIGN(4K) { *(.boot.bss) }

    . = ALIGN(4K) + KERNEL_VMA;
    .text   : AT(ADDR(.text) - KERNEL_VMA)   ALIGN(4K) { *(.text*) }
    .rodata : AT(ADDR(.rodata) - KERNEL_VMA) ALIGN(4K) { *(.rodata*) }
    .data   : AT(ADDR(.data) - KERNEL_VMA)   ALIGN(4K) { *(.data*) }
    .bss    : AT(ADDR(.bss) - KERNEL_VMA)    ALIGN(4K) { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}

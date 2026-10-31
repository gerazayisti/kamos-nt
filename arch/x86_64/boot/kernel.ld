/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Kernel link script (ADR-0002, ADR-0005). Owner: A03.
 *
 * .boot (Multiboot2 header and the 32-bit trampoline) is linked at its physical address, 1 MiB.
 * Everything else is linked at KERNEL_VMA + physical address and loaded at the physical address
 * (AT). Sections are page aligned so that the boot page tables can map them with different rights.
 * The image must fit in the first 2 MiB of physical memory (one boot page table).
 */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(KiBootEntry32)

KERNEL_VMA = 0xFFFFFFFF80000000;
KERNEL_LMA = 1M;

PHDRS
{
    boot   PT_LOAD FLAGS(5);   /* R X */
    text   PT_LOAD FLAGS(5);   /* R X */
    rodata PT_LOAD FLAGS(4);   /* R   */
    data   PT_LOAD FLAGS(6);   /* R W */
}

SECTIONS
{
    . = KERNEL_LMA;
    __boot_start = .;
    .boot : {
        KEEP(*(.multiboot2))
        *(.boot.text)
        *(.boot.data)
    } :boot
    . = ALIGN(4K);
    __boot_end = .;

    . += KERNEL_VMA;
    __kernel_start = .;

    __text_start = .;
    .text : AT(ADDR(.text) - KERNEL_VMA) {
        *(.text .text.*)
    } :text
    . = ALIGN(4K);
    __text_end = .;

    __rodata_start = .;
    .rodata : AT(ADDR(.rodata) - KERNEL_VMA) {
        *(.rodata .rodata.*)
        . = ALIGN(8);
        __kfixup_start = .;
        KEEP(*(.kfixup))
        __kfixup_end = .;
    } :rodata
    . = ALIGN(4K);
    __rodata_end = .;

    __data_start = .;
    .data : AT(ADDR(.data) - KERNEL_VMA) {
        *(.data .data.*)
    } :data
    . = ALIGN(4K);
    /* Live boot page tables: NOBITS, outside .bss so that clearing .bss does not touch them. */
    .pgtbl (NOLOAD) : AT(ADDR(.pgtbl) - KERNEL_VMA) {
        *(.pgtbl)
    } :data
    . = ALIGN(4K);
    __bss_start = .;
    .bss (NOLOAD) : AT(ADDR(.bss) - KERNEL_VMA) {
        *(.bss .bss.*)
        *(COMMON)
    } :data
    . = ALIGN(4K);
    __bss_end = .;
    .bootstack (NOLOAD) : AT(ADDR(.bootstack) - KERNEL_VMA) {
        *(.bootstack)
    } :data
    . = ALIGN(4K);
    __data_end = .;
    __kernel_end = .;

    /* Physical addresses, used by the 32-bit trampoline and by the C boot code. */
    __kernel_phys_start = __boot_start;
    __kernel_phys_end   = __kernel_end - KERNEL_VMA;
    __text_start_phys   = __text_start - KERNEL_VMA;
    __text_end_phys     = __text_end - KERNEL_VMA;
    __rodata_start_phys = __rodata_start - KERNEL_VMA;
    __rodata_end_phys   = __rodata_end - KERNEL_VMA;
    __data_start_phys   = __data_start - KERNEL_VMA;
    __data_end_phys     = __data_end - KERNEL_VMA;
    __pgtbl_phys        = LOADADDR(.pgtbl);

    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}

ASSERT(__kernel_end - KERNEL_VMA <= 2M, "kernel image does not fit in the 2 MiB boot window")

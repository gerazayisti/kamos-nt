; SPDX-License-Identifier: Apache-2.0
; Copyright (c) 2026 The Kamos-NT Authors
;
; A02 build fixture: Multiboot2 entry, switch to long mode (as in ADR-0001), call C.
; It exists to test the build rules, libgcc and GDB. It is not the kernel (A03 owns that).

MB2_MAGIC     equ 0xE85250D6
MB2_LOADER_OK equ 0x36D76289
DEBUG_EXIT    equ 0xF4

section .multiboot2 align=8
mb2_header:
    dd MB2_MAGIC
    dd 0
    dd mb2_header_end - mb2_header
    dd -(MB2_MAGIC + 0 + (mb2_header_end - mb2_header))
    dw 0
    dw 0
    dd 8
mb2_header_end:

section .boot.bss nobits alloc noexec write align=4096
pml4:  resb 4096
pdpt:  resb 4096
pd:    resb 4096

section .boot.data progbits alloc noexec nowrite align=16
gdt:
    dq 0
    dq 0x00209A0000000000       ; 64-bit code
    dq 0x0000920000000000       ; data
gdt_end:
gdt_ptr:
    dw gdt_end - gdt - 1
    dd gdt

section .boot.text progbits alloc exec nowrite align=16
bits 32
global fixture_entry
fixture_entry:
    cli
    cmp eax, MB2_LOADER_OK
    jne .bad
    ; Zero the three page-table pages.
    mov edi, pml4
    xor eax, eax
    mov ecx, 3 * 4096 / 4
    rep stosd
    ; PML4[0] and PML4[511] -> PDPT; PDPT[0] and PDPT[510] -> PD; PD = 512 x 2 MiB pages.
    mov dword [pml4], pdpt + 3
    mov dword [pml4 + 511 * 8], pdpt + 3
    mov dword [pdpt], pd + 3
    mov dword [pdpt + 510 * 8], pd + 3
    xor ecx, ecx
.fill:
    mov eax, ecx
    shl eax, 21
    or eax, 0x83                ; present, writable, 2 MiB page
    mov [pd + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .fill
    mov eax, cr4
    or eax, 1 << 5              ; PAE
    mov cr4, eax
    mov eax, pml4
    mov cr3, eax
    mov ecx, 0xC0000080         ; EFER
    rdmsr
    or eax, (1 << 8) | (1 << 11) ; LME, NXE
    wrmsr
    mov eax, cr0
    or eax, 0x80000001          ; PG, PE
    mov cr0, eax
    lgdt [gdt_ptr]
    jmp 0x08:long_entry
.bad:
    mov eax, 0x11
    out DEBUG_EXIT, eax
.halt32:
    hlt
    jmp .halt32

bits 64
long_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    mov rax, high_entry
    jmp rax

section .bss align=16
stack_bottom: resb 16384
stack_top:

section .text
bits 64
extern fixture_main
high_entry:
    lea rsp, [rel stack_top]
    xor ebp, ebp
    call fixture_main
.halt:
    hlt
    jmp .halt

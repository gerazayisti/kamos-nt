; SPDX-License-Identifier: Apache-2.0
; Copyright (c) 2026 The Kamos-NT Authors
;
; Multiboot2 header, 32-bit to 64-bit trampoline and first 64-bit instructions. Owner: A03.
; Entry state and the mappings built here: docs/architecture/boot-contract.md.
; Sources: Multiboot2 specification 2.0; Intel SDM vol. 3, ch. 4 (paging) and 9.8 (mode switch).

MB2_MAGIC     equ 0xE85250D6
MB2_LOADER_OK equ 0x36D76289
COM1          equ 0x3F8
DEBUG_EXIT    equ 0xF4

; Page table bits.
PTE_P         equ 0x1
PTE_RW        equ 0x2
PTE_PS        equ 0x80
PTE_NX_HI     equ 0x80000000            ; bit 63, in the high dword

; Physical address of a label in .pgtbl (the section is loaded at __pgtbl_phys).
%define PHYS(l) (__pgtbl_phys + ((l) - pgtbl_base))

extern __pgtbl_phys
extern __text_start_phys, __text_end_phys
extern __rodata_start_phys, __rodata_end_phys
extern __data_start_phys, __kernel_phys_end
extern __bss_start, __bss_end
extern KiStartKernel

global KiBootEntry32
global KiBootStackTop
global KiPml4, KiPdLow, KiPtLow, KiPtHigh

section .multiboot2 align=8
mb2_header:
    dd MB2_MAGIC
    dd 0                                ; architecture: i386
    dd mb2_header_end - mb2_header
    dd -(MB2_MAGIC + 0 + (mb2_header_end - mb2_header))
    dw 6, 0                             ; module alignment tag: modules on page boundaries
    dd 8
    dw 0, 0                             ; end tag
    dd 8
mb2_header_end:

; Live page tables. Zeroed by the trampoline.
section .pgtbl nobits alloc noexec write align=4096
pgtbl_base:
pml4:
KiPml4:     resb 4096
pdpt_low:   resb 4096
pd_low:
KiPdLow:    resb 4 * 4096               ; 4 GiB of 2 MiB pages
pt_low:
KiPtLow:    resb 4096                   ; first 2 MiB in 4 KiB pages
pdpt_high:  resb 4096
pd_high:    resb 4096
pt_high:
KiPtHigh:   resb 4096                   ; the kernel image, 4 KiB pages
pgtbl_end:

section .bootstack nobits alloc noexec write align=4096
boot_stack:     resb 16384
KiBootStackTop:

section .boot.data progbits alloc noexec nowrite align=16
boot_gdt:
    dq 0
    dq 0x00209A0000000000               ; 0x08: 64-bit code, DPL 0
    dq 0x0000920000000000               ; 0x10: data
boot_gdt_end:
boot_gdt_ptr:
    dw boot_gdt_end - boot_gdt - 1
    dd boot_gdt

msg_magic:  db "BOOT: not entered through Multiboot2 (EAX magic wrong)", 13, 10, 0
msg_nolm:   db "BOOT: CPU has no long mode", 13, 10, 0
msg_nonx:   db "BOOT: CPU has no NX bit", 13, 10, 0

; Fills pt_high for [%1, %2) (physical, page aligned) with the low flags %3 and high dword %4.
%macro FILL_PT_HIGH 4
    mov eax, %1
    mov ecx, %2
%%next:
    cmp eax, ecx
    jae %%done
    mov edx, eax
    shr edx, 9                          ; (phys >> 12) * 8
    mov edi, eax
    or edi, %3
    mov dword [PHYS(pt_high) + edx], edi
    mov dword [PHYS(pt_high) + edx + 4], %4
    add eax, 4096
    jmp %%next
%%done:
%endmacro

section .boot.text progbits alloc exec nowrite align=16
bits 32
KiBootEntry32:
    cli
    cld
    mov ebp, ebx                        ; keep the physical address of the boot information
    cmp eax, MB2_LOADER_OK
    mov esi, msg_magic
    jne .fail

    mov eax, 0x80000000                 ; highest extended CPUID leaf
    cpuid
    cmp eax, 0x80000001
    mov esi, msg_nolm
    jb .fail
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29                   ; long mode
    mov esi, msg_nolm
    jz .fail
    test edx, 1 << 20                   ; NX
    mov esi, msg_nonx
    jz .fail

    ; Zero the page tables.
    mov edi, PHYS(pgtbl_base)
    xor eax, eax
    mov ecx, (pgtbl_end - pgtbl_base) / 4
    rep stosd

    ; PML4[0] -> PDPT_LOW (identity), PML4[511] -> PDPT_HIGH (top 2 GiB).
    mov dword [PHYS(pml4)], PHYS(pdpt_low) + PTE_P + PTE_RW
    mov dword [PHYS(pml4) + 511 * 8], PHYS(pdpt_high) + PTE_P + PTE_RW

    ; PDPT_LOW[0..3] -> four page directories.
    xor ecx, ecx
.pdpt:
    mov eax, ecx
    shl eax, 12
    add eax, PHYS(pd_low) + PTE_P + PTE_RW
    mov [PHYS(pdpt_low) + ecx * 8], eax
    inc ecx
    cmp ecx, 4
    jne .pdpt

    ; 2048 x 2 MiB pages, identity, writable, not executable.
    xor ecx, ecx
.pd:
    mov eax, ecx
    shl eax, 21
    or eax, PTE_P + PTE_RW + PTE_PS
    mov [PHYS(pd_low) + ecx * 8], eax
    mov dword [PHYS(pd_low) + ecx * 8 + 4], PTE_NX_HI
    inc ecx
    cmp ecx, 2048
    jne .pd

    ; The first 2 MiB use 4 KiB pages: the first 64 KiB stay absent (null guard, ADR-0005). The
    ; pages are executable until the C code has run (it removes the image alias and sets NX).
    mov dword [PHYS(pd_low)], PHYS(pt_low) + PTE_P + PTE_RW
    mov dword [PHYS(pd_low) + 4], 0
    mov ecx, 16
.ptlow:
    mov eax, ecx
    shl eax, 12
    or eax, PTE_P + PTE_RW
    mov [PHYS(pt_low) + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .ptlow

    ; Kernel image: PML4[511] -> PDPT_HIGH[510] -> PD_HIGH[0] -> PT_HIGH (4 KiB pages, W^X).
    mov dword [PHYS(pdpt_high) + 510 * 8], PHYS(pd_high) + PTE_P + PTE_RW
    mov dword [PHYS(pd_high)], PHYS(pt_high) + PTE_P + PTE_RW
    FILL_PT_HIGH __text_start_phys,   __text_end_phys,   PTE_P,          0
    FILL_PT_HIGH __rodata_start_phys, __rodata_end_phys, PTE_P,          PTE_NX_HI
    FILL_PT_HIGH __data_start_phys,   __kernel_phys_end, PTE_P + PTE_RW, PTE_NX_HI

    mov eax, cr4
    or eax, 1 << 5                      ; PAE
    mov cr4, eax
    mov eax, PHYS(pml4)
    mov cr3, eax
    mov ecx, 0xC0000080                 ; EFER
    rdmsr
    or eax, (1 << 8) | (1 << 11)        ; LME, NXE
    wrmsr
    mov eax, cr0
    or eax, 0x80010021                  ; PG, WP, NE, PE
    mov cr0, eax
    lgdt [boot_gdt_ptr]
    jmp 0x08:long_entry

.fail:                                  ; ESI: message. COM1 is not programmed: QEMU does not need it.
    mov dx, COM1
.char:
    lodsb
    test al, al
    jz .exit
    out dx, al
    jmp .char
.exit:
    mov eax, 0x12                       ; bug check exit value (HAL_EXIT_BUGCHECK)
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
    mov edi, MB2_LOADER_OK              ; first argument: the magic (checked above)
    mov esi, ebp                        ; second argument: boot information
    mov rax, high_entry
    jmp rax

section .text
bits 64
high_entry:
    lea rsp, [rel KiBootStackTop]
    xor ebp, ebp
    push 0
    popfq                               ; clear DF, NT, AC, IF
    mov r8, rdi
    mov r9, rsi
    lea rdi, [rel __bss_start]
    lea rcx, [rel __bss_end]
    sub rcx, rdi
    shr rcx, 3
    xor eax, eax
    rep stosq
    mov rdi, r8
    mov rsi, r9
    call KiStartKernel
.halt:
    cli
    hlt
    jmp .halt

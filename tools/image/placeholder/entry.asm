; SPDX-License-Identifier: Apache-2.0
; Copyright (c) 2026 The Kamos-NT Authors
;
; Phase 0 placeholder kernel. Owner: A02.
; Only purpose: prove that the build, GRUB, the ISO and the QEMU test harness work.
; It checks the Multiboot2 magic, prints one line on COM1 and leaves QEMU through
; isa-debug-exit. A03 replaces it in Phase 1 (arch/x86_64/boot/).

MB2_MAGIC     equ 0xE85250D6
MB2_ARCH_I386 equ 0
MB2_LOADER_OK equ 0x36D76289
COM1          equ 0x3F8
DEBUG_EXIT    equ 0xF4

section .multiboot2 align=8
mb2_header:
    dd MB2_MAGIC
    dd MB2_ARCH_I386
    dd mb2_header_end - mb2_header
    dd -(MB2_MAGIC + MB2_ARCH_I386 + (mb2_header_end - mb2_header))
    ; End tag
    dw 0
    dw 0
    dd 8
mb2_header_end:

section .text
bits 32
global _start
_start:
    cli
    mov esi, msg_ok
    mov ebx, 0x10               ; exit value 0x10 -> QEMU exit code 33 (pass)
    cmp eax, MB2_LOADER_OK
    je .print
    mov esi, msg_bad
    mov ebx, 0x11               ; any other value is a failure
.print:
    mov dx, COM1
.next:
    lodsb
    test al, al
    jz .exit
    out dx, al
    jmp .next
.exit:
    mov eax, ebx
    out DEBUG_EXIT, eax
.halt:
    hlt
    jmp .halt

section .rodata
msg_ok:  db "Kamos-NT placeholder: Multiboot2 handoff OK", 13, 10
         db "KTEST-END pass=1 fail=0 skip=0", 13, 10, 0
msg_bad: db "Kamos-NT placeholder: bad Multiboot2 magic", 13, 10
         db "KTEST-END pass=0 fail=1 skip=0", 13, 10, 0

; SPDX-License-Identifier: Apache-2.0
; Copyright (c) 2026 The Kamos-NT Authors
;
; Interrupt and exception entry stubs (Intel SDM vol. 3, 6.14 and 6.15). Owner: A03.
; One stub per vector gives the stack the same shape: the CPU pushes SS, RSP, RFLAGS, CS, RIP and,
; for vectors 8, 10 to 14, 17, 21, 29 and 30 only, an error code. Other stubs push 0 in its place.
; Then the vector, then the registers; the result is KTRAP_FRAME (hal/halp.h).

extern KiTrapDispatch
global KiIsrTable

section .text
bits 64

%macro ISR_NOERR 1
isr_%1:
    push 0
    push %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr_%1:
    push %1
    jmp isr_common
%endmacro

%assign v 0
%rep 256
%if v == 8 || (v >= 10 && v <= 14) || v == 17 || v == 21 || v == 29 || v == 30
    ISR_ERR %[v]
%else
    ISR_NOERR %[v]
%endif
%assign v v + 1
%endrep

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld
    mov rdi, rsp                        ; KTRAP_FRAME *
    call KiTrapDispatch
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16                         ; vector and error code
    iretq

%macro ISR_ADDR 1
    dq isr_%1
%endmacro

section .rodata
align 8
KiIsrTable:
%assign v 0
%rep 256
    ISR_ADDR %[v]
%assign v v + 1
%endrep

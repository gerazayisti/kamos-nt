; SPDX-License-Identifier: Apache-2.0
; Copyright (c) 2026 The Kamos-NT Authors
;
; Instructions that fault on purpose, written in assembly so that the compiler cannot fold or
; reorder them, with a label on the faulting instruction (Fault) and one where execution resumes
; (Resume). The tests register that exact pair; the oracle for the reported rip is the label address.
; Owner: A03. System V calling convention: first argument in RDI.

section .text
bits 64

global KtDoDe, KtDeFault, KtDeResume
KtDoDe:                                 ; divide by zero: #DE, no error code
    xor ecx, ecx
    mov eax, 1
    xor edx, edx
KtDeFault:
    div ecx
KtDeResume:
    ret

global KtDoUd, KtUdFault, KtUdResume
KtDoUd:                                 ; #UD, no error code
KtUdFault:
    ud2
KtUdResume:
    ret

global KtDoBp, KtBpResume
KtDoBp:                                 ; int3 is a trap: the saved rip is the next instruction
    int3
KtBpResume:
    ret

global KtDoGpNonCanonical, KtGpNcFault, KtGpNcResume
KtDoGpNonCanonical:                     ; non-canonical address: #GP(0), not #PF
    mov rax, 0x0008000000000000
KtGpNcFault:
    mov rcx, [rax]
KtGpNcResume:
    ret

global KtDoGpSelector, KtGpSelFault, KtGpSelResume
KtDoGpSelector:                         ; load of a selector beyond the GDT limit: #GP with the selector
    mov ax, 0x1230                      ; TI = 0: the GDT (TI = 1 would read the LDT, whose limit is
KtGpSelFault:                           ; 0xFFFF after reset, so it would be a #PF at LDT base 0)
    mov ds, ax
KtGpSelResume:
    ret

global KtDoRead, KtReadFault, KtReadResume
KtDoRead:                               ; mov rax, [rdi]
KtReadFault:
    mov rax, [rdi]
KtReadResume:
    ret

global KtDoWrite, KtWriteFault, KtWriteResume
KtDoWrite:                              ; mov byte [rdi], 1
KtWriteFault:
    mov byte [rdi], 1
KtWriteResume:
    ret

global KtDoCall, KtCallRet
KtDoCall:                               ; call rdi: an instruction fetch from RDI
    call rdi
    ret
KtCallRet:                              ; resume address when the fetch faults: pops the return address
    ret

global KtDoStaticFixup, KtStaticFault, KtStaticResume
KtDoStaticFixup:                        ; the pair is in the static .kfixup table below
KtStaticFault:
    mov rax, [rdi]
KtStaticResume:
    ret

global KtDoPfRegs, KtPfRegsFault, KtPfRegsResume
KtDoPfRegs:                             ; returns 1 if rbx and r12 to r15 survive a recovered fault
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov rbx, 0x1111111111111111
    mov r12, 0x1212121212121212
    mov r13, 0x1313131313131313
    mov r14, 0x1414141414141414
    mov r15, 0x1515151515151515
KtPfRegsFault:
    mov rax, [rdi]
KtPfRegsResume:
    xor edx, edx
    mov rax, 0x1111111111111111
    cmp rbx, rax
    jne .done
    mov rax, 0x1212121212121212
    cmp r12, rax
    jne .done
    mov rax, 0x1313131313131313
    cmp r13, rax
    jne .done
    mov rax, 0x1414141414141414
    cmp r14, rax
    jne .done
    mov rax, 0x1515151515151515
    cmp r15, rax
    jne .done
    mov edx, 1
.done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    mov eax, edx
    ret

global KtDoBadStack
KtDoBadStack:                           ; a fault with an unusable stack: the CPU cannot push the
    mov rsp, 0                          ; frame of the first #PF, so the second one is a #DF
    mov rax, [rdi]
    hlt

section .kfixup progbits alloc noexec nowrite align=8
    dq KtStaticFault, KtStaticResume

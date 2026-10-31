; SPDX-License-Identifier: Apache-2.0
; Copyright (c) 2026 The Kamos-NT Authors
;
; Timer noise probe (A10). A 512-byte real-mode boot sector, NOT a kernel. It programs PIT
; channel 0 as a 100 Hz rate generator (divisor 11932), installs an IRQ0 handler (BIOS vector 8),
; waits for 1000 ticks in an hlt loop and prints two KTEST lines on COM1 with the guest's nominal
; clock (0 and 10 s in units of 100 ns). The host runner stamps the arrival of the two lines; the
; ratio guest/host is the noise floor of the "timer within 1 % over 10 s" criterion under QEMU TCG.
; It has the same shape as the real test (interrupt-driven, hlt idle). A first version that polled
; the PIT counter in a busy loop gave ratios of 0.55 to 0.82: do not poll, see
; docs/testing/oracles-wave1.md section 5. It measures the emulated PIT against the host clock,
; nothing about a real machine.
bits 16
org 0x7c00
start:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7c00
    mov word [0x20], isr      ; IRQ0 is vector 8 in the BIOS mapping
    mov word [0x22], 0
    mov al, 0x34
    out 0x43, al
    mov al, 11932 & 0xff
    out 0x40, al
    mov al, 11932 >> 8
    out 0x40, al
    mov word [cnt], 0
    sti
.w0: hlt
    cmp word [cnt], 1         ; align to a tick
    jb .w0
    mov word [cnt], 0
    mov si, msg_start
    call print
.w: hlt
    cmp word [cnt], 1000
    jb .w
    cli
    mov si, msg_end
    call print
    mov al, 0x10
    out 0xf4, al
.h: hlt
    jmp .h
isr:
    inc word [cnt]
    push ax
    mov al, 0x20
    out 0x20, al
    pop ax
    iret
print: mov dx, 0x3f8
.c: lodsb
    test al, al
    jz .d
    out dx, al
    jmp .c
.d: ret
cnt dw 0
msg_start: db "KTEST timer start PASS t100ns=0", 10, 0
msg_end:   db "KTEST timer end PASS t100ns=100000000", 10, "KTEST-END pass=2 fail=0 skip=0", 10, 0
times 510 - ($ - $$) db 0
dw 0xAA55

#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Experiment: does GDB need "set architecture" to follow QEMU from 32-bit protected mode into
# long mode? Boots the kernel-c fixture (tools/ci/fixtures/kernel-c) paused, runs GDB in batch
# mode with one of three settings, and compares what GDB disassembles with objdump -M i386
# (a different decoder). The fixture has an "inc %ecx" (opcode 0x41) that is a REX prefix when
# decoded as 64-bit code, so a wrong mode shows. Owner: A02.
# Usage: tools/debug/arch-check.sh        (needs build/fx from tools/ci/kbuild-selftest.sh)
set -uo pipefail

cd "$(dirname "$0")/../.."
export PATH="$HOME/opt/cross/bin:$PATH"
fx=build/fx
[ -f "$fx/kernel.elf" ] && [ -f "$fx/kamos.iso" ] || { echo "run tools/ci/kbuild-selftest.sh first"; exit 2; }
nm=x86_64-elf-nm; od=x86_64-elf-objdump
fill=0x$($nm $fx/kernel.elf | awk '$3=="fixture_entry.fill" {print $1}')
cr0=0x$($od -M i386 -d $fx/kernel.elf | awk '/mov +%eax,%cr0/ {sub(":","",$1); print $1}')
echo "fixture: fill loop at $fill, 'mov %eax,%cr0' at $cr0"
# Reference decoding, 32-bit, from objdump: instruction text of the first six instructions.
$od -M i386 -d --start-address="$fill" --stop-address=$((fill + 0x18)) $fx/kernel.elf |
    awk -F'\t' '/^ +[0-9a-f]+:/ {gsub(/ +/," ",$3); print $3}' >build/arch-ref.txt
echo "objdump -M i386 reference:"; cat build/arch-ref.txt

overall=0
i=0
for variant in none i386:x86-64 i386 disasm-i386; do
    i=$((i + 1)); port=$((14000 + RANDOM % 900))
    pre=()
    case "$variant" in
        none) ;;
        disasm-i386) pre=(-ex "set disassembler-options i386") ;;
        *) pre=(-ex "set architecture $variant") ;;
    esac
    echo; echo "=== variant: $variant (none = GDB defaults)"
    ISO=$fx/kamos.iso BUILD=build tools/qemu/gdb.sh --headless --timeout 60 --port "$port" >build/arch-qemu.out 2>&1 &
    qpid=$!; sleep 1.5
    timeout 40 gdb -q -batch "$fx/kernel.elf" "${pre[@]}" -ex "target remote :$port" \
        -ex "hbreak *$cr0" -ex "continue" \
        -ex "echo ---at-cr0\n" -ex "show architecture" -ex "info registers cs eflags" \
        -ex "x/6i $fill" \
        -ex "echo ---stepi-x3\n" -ex "stepi" -ex "stepi" -ex "stepi" \
        -ex "show architecture" -ex "info registers rip cs" -ex "x/2i \$pc" \
        -ex "hbreak fixture_main" -ex "continue" \
        -ex "echo ---in-C\n" -ex "show architecture" -ex "info registers rip rsp" -ex "x/2i \$pc" -ex "bt 2" \
        >"build/arch-$i.out" 2>&1
    echo "gdb exit code: $?"
    kill $qpid 2>/dev/null; wait $qpid 2>/dev/null
    cat "build/arch-$i.out"
    # Oracle: the six lines GDB printed for x/6i at the fill loop vs objdump -M i386.
    awk '/^---at-cr0/ {on=1} /^---stepi/ {on=0} on && /<fixture_entry.fill/ {p=1} p && on && /\t/ {n++; if (n<=6) {sub(/^[^\t]*\t/,""); gsub(/ +/," "); print}}' \
        "build/arch-$i.out" >"build/arch-gdb-$i.txt"
    if grep -q "Remote 'g' packet reply is too long" "build/arch-$i.out"; then
        echo "VERDICT[$variant]: GDB could not attach (g packet too long); the listing above came from the ELF file, not from the live target"
    elif [ -s "build/arch-gdb-$i.txt" ] && cmp -s "build/arch-gdb-$i.txt" build/arch-ref.txt; then
        echo "VERDICT[$variant]: 32-bit code decoded like objdump -M i386"
    else
        echo "VERDICT[$variant]: DIFFERS from objdump (or no output); GDB lines:"; cat "build/arch-gdb-$i.txt"
    fi
    if grep -q "fixture_main () at" "build/arch-$i.out"; then
        echo "VERDICT[$variant]: reached C code in long mode"
    else echo "VERDICT[$variant]: did NOT reach C code in long mode"; fi
done

#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Verifies every tool the build needs (Annex A.3). Owner: A02.
# Usage: check-toolchain.sh <cross-prefix>   e.g. ~/opt/cross/bin/x86_64-elf-
set -uo pipefail

cross=${1:-$HOME/opt/cross/bin/x86_64-elf-}
missing=0

check() {
    if command -v "$1" >/dev/null 2>&1; then
        [ "$1" = grub-file ] && { printf "OK       %-28s %s
" "$1" "(no --version)"; return; }
        printf 'OK       %-28s %s\n' "$1" "$("$1" --version 2>&1 | head -n1)"
    else
        printf 'MISSING  %s\n' "$1"
        missing=$((missing + 1))
    fi
}

for t in nasm qemu-system-x86_64 grub-mkrescue grub-file xorriso mformat gdb make tar \
         readelf python3 clang lld-link x86_64-w64-mingw32-gcc; do
    check "$t"
done
check "${cross}gcc"
check "${cross}ld"

if command -v "${cross}gcc" >/dev/null 2>&1; then
    want=$(grep -E '^gcc=' "$(dirname "$0")/../toolchain/versions.txt" | cut -d= -f2)
    have=$("${cross}gcc" -dumpversion)
    if [ "$want" != "$have" ]; then
        echo "MISMATCH x86_64-elf-gcc: want $want, have $have"
        missing=$((missing + 1))
    fi
fi

if [ "$missing" -ne 0 ]; then
    echo "toolchain-check: $missing problem(s)"
    exit 1
fi
echo "toolchain-check: OK"

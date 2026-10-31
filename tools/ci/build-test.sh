#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# CI and local check: build the kernel and the ISO with the cross toolchain, run the
# structural checks of ADR-0002, then boot it headless under QEMU. Owner: A02.
# Usage: tools/ci/build-test.sh <qemu-cpu>      (for example qemu64 or max)
set -euo pipefail

cpu=${1:?usage: build-test.sh <qemu-cpu>}
cd "$(dirname "$0")/../.."
export PATH="$HOME/opt/cross/bin:$PATH"
cross=${CROSS:-x86_64-elf-}

make toolchain-check CROSS="$cross"
make clean
make all iso CROSS="$cross"

# ADR-0002: static kernel, no dynamic section, no undefined symbol.
if ! readelf -d build/kernel.elf 2>&1 | grep -q 'no dynamic section'; then
    echo "FAIL: kernel.elf has a dynamic section"; exit 1
fi
undefined=$("${cross}nm" -u build/kernel.elf)
if [ -n "$undefined" ]; then
    echo "FAIL: kernel.elf has undefined symbols:"; echo "$undefined"; exit 1
fi

make test CROSS="$cross" QEMU_CPU="$cpu"
make package CROSS="$cross"
echo "build-test ($cpu): PASS"

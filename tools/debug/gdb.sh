#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Attaches GDB to a QEMU started with tools/qemu/gdb.sh. Owner: A02.
# Usage: tools/debug/gdb.sh [port] [extra gdb arguments...]     (default port 1234)
#        KERNEL=build/kernel.elf   image with symbols
set -euo pipefail
cd "$(dirname "$0")/../.."
port=${1:-1234}; [ $# -gt 0 ] && shift
kernel=${KERNEL:-build/kernel.elf}
args=(-q "$kernel")
args+=(-ex "target remote :$port" -x tools/debug/kamos.gdb)
exec gdb "${args[@]}" "$@"

#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Shared part of the QEMU profiles (plan, Annex A.5). Owner: A02. Sourced, not run.
#
# Options understood by every profile script:
#   --cpu <model>      qemu64 (default) or max          (env QEMU_CPU)
#   --mem <size>       default 512M                     (env QEMU_MEM)
#   --timeout <s>      kill QEMU after s seconds        (env TIMEOUT_S; 0 = no limit)
#   --headless         add -display none
#   --print            print the QEMU command line and exit without starting QEMU
# Environment: ISO (default build/kamos.iso), BUILD (default build), QEMU.
# Exit code: that of QEMU, 124 on timeout.

kq_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$kq_root"
BUILD=${BUILD:-build}
ISO=${ISO:-$BUILD/kamos.iso}
QEMU=${QEMU:-qemu-system-x86_64}
kq_cpu=${QEMU_CPU:-qemu64}
kq_mem=${QEMU_MEM:-512M}
kq_timeout=${TIMEOUT_S:-0}
kq_headless=0
kq_print=0
kq_rest=()

kq_parse() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --cpu)      kq_cpu=$2; shift 2 ;;
            --mem)      kq_mem=$2; shift 2 ;;
            --timeout)  kq_timeout=$2; shift 2 ;;
            --headless) kq_headless=1; shift ;;
            --print)    kq_print=1; shift ;;
            *)          kq_rest+=("$1"); shift ;;
        esac
    done
}

# kq_run <profile options...>: add the common options and start (or print) QEMU.
kq_run() {
    local cmd=("$QEMU" -machine q35 -cpu "$kq_cpu" -m "$kq_mem" -cdrom "$ISO")
    [ "$kq_headless" = 1 ] && cmd+=(-display none)
    cmd+=("$@")
    if [ "$kq_print" = 1 ]; then printf '%s ' "${cmd[@]}"; echo; return 0; fi
    [ -f "$ISO" ] || { echo "kq: $ISO not found (run make iso)" >&2; return 2; }
    mkdir -p "$BUILD"
    if [ "$kq_timeout" != 0 ]; then exec timeout --foreground "$kq_timeout" "${cmd[@]}"; fi
    exec "${cmd[@]}"
}

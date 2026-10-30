#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Headless QEMU test run (ADR-0010). Owner: A02 (harness), A10 (protocol).
# Usage: run-test.sh <iso> <serial-log> <timeout-seconds> <cpu> <memory>
# Exit 0 only if QEMU exits with 33 (kernel wrote 0x10 to isa-debug-exit)
# and the serial log ends with KTEST-END ... fail=0.
set -euo pipefail

iso=$1 log=$2 timeout_s=$3 cpu=$4 mem=$5

rc=0
timeout --foreground "$timeout_s" qemu-system-x86_64 \
    -machine q35 -cpu "$cpu" -m "$mem" -cdrom "$iso" \
    -display none -serial "file:$log" -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 || rc=$?

echo "--- serial log ($log) ---"
cat "$log" || true
echo "--- qemu exit code: $rc ---"

if [ "$rc" -eq 124 ]; then
    echo "FAIL: timeout after ${timeout_s}s"
    exit 1
fi
if [ "$rc" -ne 33 ]; then
    echo "FAIL: expected QEMU exit code 33, got $rc"
    exit 1
fi
if ! grep -Eq '^KTEST-END pass=[0-9]+ fail=0 ' "$log"; then
    echo "FAIL: no clean KTEST-END line"
    exit 1
fi
echo "PASS"

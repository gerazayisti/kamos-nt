#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Headless QEMU test run (ADR-0010). Owner: A02 (harness), A10 (protocol and judge).
# Usage: run-test.sh <iso> <serial-log> <timeout-seconds> <cpu> <memory> [manifest]
# QEMU leaves through isa-debug-exit (hal.h): exit 33 = tests passed, 35 = tests failed,
# 37 = bugcheck. The verdict comes from tests/framework/ktest-run.py, which reads the serial log
# and the QEMU exit code: a log without any KTEST line, a count that disagrees with the lines, or
# an exit code that disagrees with the END line is a failure. Exit status: 0 only for PASS.
set -euo pipefail

iso=$1 log=$2 timeout_s=$3 cpu=$4 mem=$5 manifest=${6:-}
cd "$(dirname "$0")/../.."

rc=0
timeout --foreground "$timeout_s" qemu-system-x86_64 \
    -machine q35 -cpu "$cpu" -m "$mem" -cdrom "$iso" \
    -display none -serial "file:$log" -no-reboot -accel tcg \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 || rc=$?

echo "--- serial log ($log) ---"
cat "$log" || true
echo "--- qemu exit code: $rc ---"
case "$rc" in
    124) echo "timeout after ${timeout_s}s" ;;
    33)  echo "QEMU exit 33: kernel reports all tests passed" ;;
    35)  echo "QEMU exit 35: kernel reports failed tests" ;;
    37)  echo "QEMU exit 37: kernel stopped (bugcheck or failed ASSERT)" ;;
esac

args=(parse "$log" --exit "$rc")
[ -n "$manifest" ] && args+=(--manifest "$manifest")
if python3 tests/framework/ktest-run.py "${args[@]}"; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

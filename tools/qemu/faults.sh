#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# QEMU profile "diagnosis of CPU faults: interrupts and resets logged in $BUILD/qemu.log". Owner: A02. Options: see common.sh.
set -euo pipefail
. "$(dirname "$0")/common.sh"
kq_parse "$@"
rm -f "$BUILD/qemu.log"
kq_run -serial stdio -d int,cpu_reset -D "$BUILD/qemu.log" -no-reboot -no-shutdown

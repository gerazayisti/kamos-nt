#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# QEMU profile "serial only, log captured in $BUILD/serial.log". Owner: A02. Options: see common.sh.
set -euo pipefail
. "$(dirname "$0")/common.sh"
kq_parse "$@"
rm -f "$BUILD/serial.log"
kq_run -display none -serial "file:$BUILD/serial.log" -no-reboot

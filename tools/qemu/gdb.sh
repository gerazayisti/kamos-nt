#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# QEMU profile "GDB: QEMU waits for a debugger on tcp port 1234 (--port N to change)". Owner: A02. Options: see common.sh.
set -euo pipefail
. "$(dirname "$0")/common.sh"
kq_parse "$@"
port=1234
if [ "${kq_rest[0]:-}" = "--port" ]; then port=${kq_rest[1]}; fi
kq_run -serial stdio -gdb "tcp::$port" -S

#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# QEMU profile "virtual disk $BUILD/disk.img and monitor on telnet 127.0.0.1:5555 (--mport N to change)". Owner: A02. Options: see common.sh.
set -euo pipefail
. "$(dirname "$0")/common.sh"
kq_parse "$@"
mport=5555
if [ "${kq_rest[0]:-}" = "--mport" ]; then mport=${kq_rest[1]}; fi
[ "$kq_print" = 1 ] || { [ -f "$BUILD/disk.img" ] || { mkdir -p "$BUILD"; truncate -s 16M "$BUILD/disk.img"; }; }
kq_run -drive "file=$BUILD/disk.img,format=raw" -monitor "telnet:127.0.0.1:$mport,server,nowait"

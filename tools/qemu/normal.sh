#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# QEMU profile "normal start (a window opens)". Owner: A02. Options: see common.sh.
set -euo pipefail
. "$(dirname "$0")/common.sh"
kq_parse "$@"
kq_run

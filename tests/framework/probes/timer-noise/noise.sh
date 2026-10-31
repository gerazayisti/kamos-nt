#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
# Timer noise floor: noise.sh <runs> <cpu>. Prints one ratio per run, then min/max/mean.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
runs=${1:-20}; cpu=${2:-qemu64}
mkdir -p "$here/../../../../build"; nasm -f bin -o "$here/../../../../build/probe.bin" "$here/boot.asm"
for i in $(seq "$runs"); do
    python3 "$here/../../ktest-run.py" qemu --disk "$here/../../../../build/probe.bin" --machine pc --cpu "$cpu" --extra=-nic --extra=none --timeout 40 \
        --manifest "$here/timer.manifest" | grep '^ratio ' | sed 's/.*ratio \([0-9.]*\) (.*/\1/'
done | tee /dev/stderr | awk '{s+=$1; if(NR==1||$1<mn)mn=$1; if(NR==1||$1>mx)mx=$1} END{printf "runs=%d min=%.6f max=%.6f mean=%.6f spread=%.4f%%\n",NR,mn,mx,s/NR,(mx-mn)*100}'

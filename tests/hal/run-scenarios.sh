#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Boots the built ISO once per scenario and judges each serial log with ktest-run.py and the
# manifest of the scenario. Owner: A03. The scenario is passed to the kernel through QEMU fw_cfg
# (opt/kamos/test); "all" is the full suite and is the default boot of `make test`.
# Usage: tests/hal/run-scenarios.sh <debug|release> <qemu64|max> [scenario ...]
# The manifest is tests/hal/<scenario>-<profile>.manifest, or tests/hal/<scenario>.manifest when
# the expectation does not depend on the profile. Exit status 0 only if every scenario held.
set -u

profile=${1:?usage: run-scenarios.sh <debug|release> <qemu64|max> [scenario ...]}
cpu=${2:?cpu}
shift 2
cd "$(dirname "$0")/../.."
iso=build/$profile/kamos.iso
[ -f "$iso" ] || { echo "build $iso first (make KAMOS_DEBUG=... iso)"; exit 2; }

if [ $# -eq 0 ]; then
    set -- all $(ls tests/hal/*.manifest | sed -E 's|tests/hal/||; s|(-(debug\|release))?\.manifest$||' | sort -u | grep -vx all)
fi

status=0
for s in "$@"; do
    man=tests/hal/$s-$profile.manifest
    [ -f "$man" ] || man=tests/hal/$s.manifest
    [ -f "$man" ] || { echo "no manifest for scenario $s"; status=1; continue; }
    log=build/$profile/scenario-$s-$cpu.log
    extra=()
    [ "$s" != all ] && extra=(--extra=-fw_cfg "--extra=name=opt/kamos/test,string=$s")
    timeout_s=${KT_TIMEOUT:-40}
    echo "=== scenario $s ($profile, $cpu), manifest $man"
    python3 tests/framework/ktest-run.py qemu --iso "$iso" --cpu "$cpu" --timeout "$timeout_s" \
        --manifest "$man" --log "$log" "${extra[@]}" > "$log.out" 2>&1
    rc=$?
    tail -n 3 "$log.out"
    [ $rc -eq 0 ] || { status=1; echo "scenario $s: FAIL (see $log.out)"; }
done
[ $status -eq 0 ] && echo "run-scenarios ($profile, $cpu): PASS" || echo "run-scenarios ($profile, $cpu): FAIL"
exit $status

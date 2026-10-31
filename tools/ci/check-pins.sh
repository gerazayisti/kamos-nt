#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Every "uses:" in .github/workflows/ must name a full 40-hex commit SHA (threat model B6).
# Local actions (./path) are allowed. Owner: A02.
# Usage: tools/ci/check-pins.sh [--selftest]
set -uo pipefail

cd "$(dirname "$0")/../.."
check() { # file...: print the offending lines, return 1 if any
    local bad=0 line
    while IFS= read -r line; do
        spec=$(sed -E 's/^[^:]*:[0-9]+:[[:space:]]*-?[[:space:]]*uses:[[:space:]]*([^[:space:]#]+).*/\1/' <<<"$line")
        case "$spec" in ./*) continue ;; esac
        if ! grep -Eq '@[0-9a-f]{40}$' <<<"$spec"; then echo "not pinned: $line"; bad=1; fi
    done < <(grep -nE '^[[:space:]]*-?[[:space:]]*uses:' "$@" /dev/null | sed -E 's/^([^:]*):([0-9]+):/\1:\2:/')
    return $bad
}

if [ "${1:-}" = "--selftest" ]; then
    tmp=$(mktemp); trap 'rm -f "$tmp"' EXIT
    printf '      - uses: actions/checkout@v4\n' >"$tmp"
    check "$tmp" >/dev/null && { echo "FAIL  a tag-pinned line was accepted"; exit 1; }
    printf '      - uses: actions/checkout@%s # v4.4.0\n' 11d5960a326750d5838078e36cf38b85af677262 >"$tmp"
    check "$tmp" >/dev/null || { echo "FAIL  a SHA-pinned line was rejected"; exit 1; }
    echo "check-pins selftest: PASS"; exit 0
fi

check .github/workflows/*.yml && echo "check-pins: PASS ($(grep -hcE '^[[:space:]]*-?[[:space:]]*uses:' .github/workflows/*.yml | paste -sd+ | bc) uses, all pinned)"

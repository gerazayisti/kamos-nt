#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Static analysis with cppcheck and format check with clang-format. Owner: A02.
# Usage: tools/ci/static-analysis.sh              analyse the kernel sources (boot/, arch/x86_64/,
#                                                 hal/, ntoskrnl/) and the build fixture
#        tools/ci/static-analysis.sh --selftest   prove that both tools fail on a planted defect
#                                                 and stay silent on a clean file
# sdk/include (A01) is not formatted by this script, see .clang-format.
set -uo pipefail

cd "$(dirname "$0")/../.."
dirs="boot arch/x86_64 hal ntoskrnl tools/ci/fixtures/kernel-c"
cppcheck_opts=(--std=c11 --platform=unix64 --enable=warning,portability --error-exitcode=1
               --inline-suppr --quiet -Isdk/include --suppress=missingIncludeSystem)

list() { git ls-files -- $dirs | grep -E '\.(c|h)$' || true; }

run_cppcheck() { cppcheck "${cppcheck_opts[@]}" "$@"; }
run_format()   { clang-format --style=file:.clang-format --dry-run --Werror "$@"; }

if [ "${1:-}" = "--selftest" ]; then
    fail=0
    d=tools/ci/fixtures/analysis
    out=$(run_cppcheck $d/bad.c 2>&1); rc=$?
    for kind in nullPointer arrayIndexOutOfBounds uninitvar; do
        grep -q "$kind" <<<"$out" && echo "PASS  cppcheck reports $kind" || { echo "FAIL  cppcheck missed $kind"; fail=1; }
    done
    [ "$rc" -ne 0 ] && echo "PASS  cppcheck exits non-zero on bad.c" || { echo "FAIL  exit 0 on bad.c"; fail=1; }
    run_cppcheck $d/good.c && echo "PASS  cppcheck silent on good.c" || { echo "FAIL  cppcheck complains on good.c"; fail=1; }
    tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
    printf 'int f(int a){\n\treturn a  +1;}\n' >"$tmp/bad_format.c"
    printf 'int f(int a)\n{\n    return a + 1;\n}\n' >"$tmp/good_format.c"
    run_format "$tmp/bad_format.c" 2>/dev/null && { echo "FAIL  clang-format accepted bad_format.c"; fail=1; } || echo "PASS  clang-format rejects bad_format.c"
    run_format "$tmp/good_format.c" && echo "PASS  clang-format accepts good_format.c" || { echo "FAIL  clang-format rejects good_format.c"; fail=1; }
    [ "$fail" -eq 0 ] && echo "static-analysis selftest: PASS" || echo "static-analysis selftest: FAIL"
    exit $fail
fi

files=$(list)
[ -n "$files" ] || { echo "static-analysis: no C file to analyse"; exit 0; }
echo "static-analysis: $(wc -l <<<"$files") file(s)"
status=0
# shellcheck disable=SC2086
run_cppcheck $files || status=1
# shellcheck disable=SC2086
run_format $files || status=1
[ "$status" -eq 0 ] && echo "static-analysis: PASS" || echo "static-analysis: FAIL"
exit $status

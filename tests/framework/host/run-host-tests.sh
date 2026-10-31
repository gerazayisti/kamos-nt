#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Host test harness. Owner: A10. Usage: tests/framework/host/run-host-tests.sh [compiler...]
# Default compilers: gcc and clang, whichever exist.
#   1. parser fixtures (hand-written verdicts)
#   2. canaries: the sanitizers must kill three broken programs and leave a correct one alone;
#      if one canary survives the harness fails before it runs any test
#   3. freestanding check of ktest.h with the kernel flags (only when the cross compiler exists)
#   4. every tests/*/host/test_*.c, built with its .srcs, run, and judged by ktest-run.py
#   5. negative programs: the framework must report their failures
# Exit status 0 only if everything above held.
set -u

cd "$(dirname "$0")/../../.."
root=$PWD
runner="python3 $root/tests/framework/ktest-run.py"
out=$root/build/host
mkdir -p "$out"
status=0
fail() { echo "HARNESS FAIL: $*"; status=1; }

compilers=("$@")
if [ ${#compilers[@]} -eq 0 ]; then
    for c in gcc clang; do command -v "$c" >/dev/null 2>&1 && compilers+=("$c"); done
fi
[ ${#compilers[@]} -gt 0 ] || { echo "no host compiler found"; exit 1; }

san="-fsanitize=address,undefined -fno-sanitize-recover=all"
cflags="-std=c11 -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer $san -I$root/tests/framework/include -I$root/sdk/include"
export ASAN_OPTIONS=detect_leaks=1:exitcode=99
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:exitcode=98

echo "== 1. parser fixtures"
$runner selftest "$root/tests/framework/fixtures" | tail -3 || fail "parser fixtures"
$runner selftest "$root/tests/framework/fixtures" >/dev/null || fail "parser fixtures"

for cc in "${compilers[@]}"; do
    d=$out/$cc; mkdir -p "$d"
    echo "== [$cc] $($cc --version | head -n1)"

    echo "-- 2. canaries"
    canary() { # name expected-substring
        local src=$root/tests/framework/host/canary/$1.c
        if ! $cc $cflags -o "$d/canary-$1" "$src" 2>"$d/canary-$1.build"; then
            fail "[$cc] canary $1 does not build"; return; fi
        "$d/canary-$1" >"$d/canary-$1.out" 2>"$d/canary-$1.err"; local rc=$?
        if [ "$rc" -ne 0 ] && grep -Eq "$2" "$d/canary-$1.err"; then
            echo "canary $1: killed as expected (rc=$rc, found '$2')"
        else
            fail "[$cc] canary $1 SURVIVED or died for another reason (rc=$rc); sanitizers are not working"
        fi
    }
    # gcc 13 reports this one first through UBSan (object-size), clang through ASan; either is a kill.
    canary heap_overflow   "heap-buffer-overflow|insufficient space for an object"
    canary signed_overflow "signed integer overflow"
    canary leak            "detected memory leaks"
    if $cc $cflags -o "$d/canary-clean" "$root/tests/framework/host/canary/clean.c" &&
       "$d/canary-clean" >/dev/null 2>"$d/canary-clean.err" && [ ! -s "$d/canary-clean.err" ]; then
        echo "canary clean: passes untouched (positive control)"
    else
        fail "[$cc] the correct program was rejected"
    fi

    echo "-- 4. tests"
    n=0; bad=0
    for t in "$root"/tests/*/host/test_*.c; do
        [ -e "$t" ] || continue
        name=$(basename "$t" .c); dir=$(dirname "$t"); n=$((n+1))
        srcs=""
        if [ -f "$dir/$name.srcs" ]; then
            while IFS= read -r s; do [ -n "$s" ] && srcs="$srcs $root/$s"; done < "$dir/$name.srcs"
        fi
        if ! $cc $cflags -o "$d/$name" "$t" $srcs 2>"$d/$name.build"; then
            cat "$d/$name.build"; fail "[$cc] $name does not build"; bad=$((bad+1)); continue; fi
        "$d/$name" >"$d/$name.log" 2>"$d/$name.err"; rc=$?
        if grep -qE 'Sanitizer|runtime error' "$d/$name.err" || [ "$rc" -ge 90 ]; then
            cat "$d/$name.err" | head -20; fail "[$cc] $name: sanitizer report (rc=$rc)"; bad=$((bad+1)); continue; fi
        code=33; [ "$rc" -ne 0 ] && code=35
        man=""; [ -f "$dir/$name.manifest" ] && man="--manifest $dir/$name.manifest"
        if $runner parse "$d/$name.log" --exit $code $man >"$d/$name.verdict"; then
            echo "test $name: $(tail -1 "$d/$name.log")"
        else
            cat "$d/$name.verdict"; fail "[$cc] $name: protocol/verdict"; bad=$((bad+1)); fi
    done
    echo "tests [$cc]: $n program(s), $bad bad"

    echo "-- 5. negative programs"
    for t in "$root"/tests/framework/host/negative/*.c; do
        name=$(basename "$t" .c)
        $cc $cflags -o "$d/neg-$name" "$t" || { fail "[$cc] negative $name does not build"; continue; }
        "$d/neg-$name" >"$d/neg-$name.log" 2>"$d/neg-$name.err"; rc=$?
        if [ "$rc" -ne 1 ]; then fail "[$cc] negative $name exited $rc, expected 1"; continue; fi
        if $runner parse "$d/neg-$name.log" --exit 35 --manifest "${t%.c}.manifest" >"$d/neg-$name.verdict"; then
            echo "negative $name: failures reported as expected"
        else
            cat "$d/neg-$name.verdict"; fail "[$cc] negative $name: wrong report"
        fi
    done
done

echo "== 3. freestanding check of ktest.h"
cross=${CROSS:-$HOME/opt/cross/bin/x86_64-elf-}
if command -v "${cross}gcc" >/dev/null 2>&1; then
    kflags="-std=c11 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-red-zone -mcmodel=kernel -mgeneral-regs-only -fno-omit-frame-pointer -Wall -Wextra -Werror -O2 -I$root/tests/framework/include -I$root/sdk/include"
    if ${cross}gcc $kflags -c -o "$out/ktest_freestanding.o" "$root/tests/framework/host/ktest_freestanding.c"; then
        und=$(${cross}nm -u "$out/ktest_freestanding.o" | awk '{print $2}' | tr '\n' ' ')
        if [ "$und" = "kt_sink " ]; then echo "freestanding: compiles with kernel flags, undefined symbols: $und"
        else fail "freestanding: unexpected undefined symbols: $und"; fi
    else fail "freestanding: ktest.h does not compile with the kernel flags"; fi
else
    echo "freestanding: SKIPPED, no cross compiler at ${cross}gcc"
fi

if [ "$status" -eq 0 ]; then echo "test-host: PASS"; else echo "test-host: FAIL"; fi
exit $status

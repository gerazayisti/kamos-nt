#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Proves the pipeline notices a broken source: a copy of HEAD gets an invalid instruction
# appended, and the build must fail. This script passes only when the build fails. Owner: A02.
set -euo pipefail

cd "$(dirname "$0")/../.."
export PATH="$HOME/opt/cross/bin:$PATH"
cross=${CROSS:-x86_64-elf-}
tmp=$(mktemp -d)
w=$tmp/broken
trap 'git worktree remove --force "$w" 2>/dev/null || true; rm -rf "$tmp"' EXIT
fail=0

git worktree add -q --detach "$w" HEAD

# Positive control: the unbroken tree builds, so the failures below come from the broken source.
if ! (cd "$w" && make all CROSS="$cross" >"$tmp/control.log" 2>&1); then
    echo "FAIL: the unbroken tree does not build (control)"; tail -5 "$tmp/control.log"; exit 1
fi
echo "ok    control: the unbroken tree builds"

# Each check breaks one file that the configuration really builds, expects the build to fail, and
# restores the file with git checkout.
expect_failure() { # label, file, command-appended-text, make args...
    local label=$1 file=$2 text=$3; shift 3
    printf '%s\n' "$text" >> "$w/$file"
    if (cd "$w" && make clean >/dev/null 2>&1; make all CROSS="$cross" "$@" >/dev/null 2>&1); then
        echo "FAIL: the build succeeded with a broken $label"; fail=1
    else
        echo "ok    $label: the build rejected the broken source"
    fi
    (cd "$w" && git checkout -q -- "$file")
}
expect_failure "boot.asm (default kernel)" arch/x86_64/boot/boot.asm 'this_is_not_an_instruction rax,'
expect_failure "hal/console.c (default kernel)" hal/console.c 'int broken_syntax( { return ; }'
expect_failure "entry.asm (placeholder, KERNEL_PLACEHOLDER=1)" tools/image/placeholder/entry.asm \
    'this_is_not_an_instruction rax,' KERNEL_PLACEHOLDER=1

if [ "$fail" -ne 0 ]; then exit 1; fi
echo "negative-selftest: PASS (the build rejected each broken source)"

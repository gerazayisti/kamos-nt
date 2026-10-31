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

git worktree add -q --detach "$w" HEAD
echo 'this_is_not_an_instruction rax,' >> "$w/tools/image/placeholder/entry.asm"

if (cd "$w" && make all CROSS="$cross" >/dev/null 2>&1); then
    echo "FAIL: the build succeeded on a broken source"; exit 1
fi
echo "negative-selftest: PASS (the build rejected the broken source)"

#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Builds the committed HEAD twice, in two worktrees with different paths and a pause in
# between, and requires byte-identical kernel.elf and ISO (ADR-0003). Owner: A02.
# Uncommitted changes are not part of the check: it works on HEAD.
set -euo pipefail

cd "$(dirname "$0")/../.."
export PATH="$HOME/opt/cross/bin:$PATH"
cross=${CROSS:-x86_64-elf-}
tmp=$(mktemp -d)
a=$tmp/repro-first
b=$tmp/another-path-second
cleanup() { git worktree remove --force "$a" 2>/dev/null || true
            git worktree remove --force "$b" 2>/dev/null || true
            rm -rf "$tmp"; }
trap cleanup EXIT

git worktree add -q --detach "$a" HEAD
git worktree add -q --detach "$b" HEAD
(cd "$a" && make clean all iso CROSS="$cross" >/dev/null)
sleep 2
(cd "$b" && make clean all iso CROSS="$cross" >/dev/null)

status=0
for f in kernel.elf kamos.iso; do
    if cmp -s "$a/build/$f" "$b/build/$f"; then
        echo "identical: $f $(sha256sum "$a/build/$f" | cut -c1-16)"
    else
        echo "FAIL: $f differs between the two builds"; status=1
    fi
done
exit $status

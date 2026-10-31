#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Usage: tools/ci/reproducible.sh [debug|release]
# Builds the committed HEAD twice, in two worktrees with different paths and a pause in
# between, and requires byte-identical kernel.elf and ISO (ADR-0003). Owner: A02.
# Uncommitted changes are not part of the check: it works on HEAD.
set -euo pipefail

cd "$(dirname "$0")/../.."
export PATH="$HOME/opt/cross/bin:$PATH"
cross=${CROSS:-x86_64-elf-}
# KAMOS_MAKE_ARGS and KAMOS_BUILD_DIR let the same check run on the kernel-c fixture.
profile=${1:-default}   # default, debug or release
case "$profile" in
    default) pargs=;              pdir=build ;;
    debug)   pargs=KAMOS_DEBUG=1; pdir=build/debug ;;
    release) pargs=KAMOS_DEBUG=0; pdir=build/release ;;
    *) echo "unknown profile $profile"; exit 2 ;;
esac
margs="$pargs ${KAMOS_MAKE_ARGS:-}"
bdir=${KAMOS_BUILD_DIR:-$pdir}
tmp=$(mktemp -d)
a=$tmp/repro-first
b=$tmp/another-path-second
cleanup() { git worktree remove --force "$a" 2>/dev/null || true
            git worktree remove --force "$b" 2>/dev/null || true
            rm -rf "$tmp"; }
trap cleanup EXIT

git worktree add -q --detach "$a" HEAD
git worktree add -q --detach "$b" HEAD
(cd "$a" && make clean all iso CROSS="$cross" $margs >/dev/null)
sleep 2
(cd "$b" && make clean all iso CROSS="$cross" $margs >/dev/null)

status=0
for f in kernel.elf kamos.iso; do
    if cmp -s "$a/$bdir/$f" "$b/$bdir/$f"; then
        echo "identical: $f $(sha256sum "$a/$bdir/$f" | cut -c1-16)"
    else
        echo "FAIL: $f differs between the two builds"; status=1
        # Diagnostics, so that a remote failure can be explained from the log alone.
        echo "  sha256 first : $(sha256sum "$a/$bdir/$f" | cut -c1-32)"
        echo "  sha256 second: $(sha256sum "$b/$bdir/$f" | cut -c1-32)"
        echo "  differing bytes: $(cmp -l "$a/$bdir/$f" "$b/$bdir/$f" | wc -l)"
        echo "  first offsets (decimal, 1-based, octal values):"
        cmp -l "$a/$bdir/$f" "$b/$bdir/$f" | head -40 | sed 's/^/    /' || true
        echo "  GRUB platforms: $(ls /usr/lib/grub 2>/dev/null | tr '\n' ' ')"
        echo "  $(xorriso -version 2>&1 | head -1)"
    fi
done
# No build path may leak into the kernel. The strings go through a variable: with pipefail,
# "strings | grep -q" would report failure when grep stops reading early.
for build in "$a" "$b"; do
    kstrings=$(strings -a "$build/$bdir/kernel.elf")
    for w in "$a" "$b" "$HOME"; do
        if grep -qF -- "$w" <<<"$kstrings"; then
            echo "FAIL: kernel.elf of $build contains the path $w"; status=1
        fi
    done
done
exit $status

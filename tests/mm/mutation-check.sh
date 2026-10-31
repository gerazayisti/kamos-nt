#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Mutation check for ntoskrnl/mm. Owner: A04.
# Each mutation breaks one rule in a private copy of the sources; at least one host test must
# then fail. A mutation that no test notices is reported as SURVIVED and the script exits 1.
# usage: tests/mm/mutation-check.sh [name-substring]   (needs gcc; the optional argument runs only the matching mutations)
set -u
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
tests="frames pagetable space pool random"
san="-fsanitize=address,undefined -fno-sanitize-recover=all"
cflags="-std=c11 -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer $san -Itests/framework/include -Isdk/include"
export ASAN_OPTIONS=detect_leaks=0:exitcode=99 UBSAN_OPTIONS=halt_on_error=1:exitcode=98

# name|file|perl expression
mutations=(
  "no W^X check|ntoskrnl/mm/mm_pagetable.c|s/if \(\(Prot & MM_PROT_WRITE\) && \(Prot & MM_PROT_EXECUTE\)\)/if (0)/"
  "no user-frame state check|ntoskrnl/mm/mm_pagetable.c|s/p == NULL \|\| p->State != MmFrameActive\)/p == NULL)/"
  "no rollback of leaves|ntoskrnl/mm/mm_pagetable.c|s/(Roll back.*?)\*leaf = 0;/\$1/s"
  "no pruning after rollback|ntoskrnl/mm/mm_pagetable.c|s/(Roll back.*?)MiPruneRange\(mm, Space->Root, Virtual, Count, kernel\);/\$1/s"
  "no TLB invalidation|ntoskrnl/mm/mm_pagetable.c|s/hw->InvalidatePage\(hw->Context, Virtual\);/(void)Virtual;/"
  "address-space lock taken at rank 70|ntoskrnl/mm/mm_pagetable.c|s/LOCK_RANK_ADDRESS_SPACE/LOCK_RANK_FRAME_DB/g"
  "double free not detected|ntoskrnl/mm/mm_frame.c|s/if \(p->State == MmFrameFree\)/if (0)/"
  "pool trailer not checked|ntoskrnl/mm/mm_pool.c|s/if \(payload\[size \+ i\] != /if (0 && payload[size + i] != /"
)

survived=0
for m in "${mutations[@]}"; do
  IFS='|' read -r name file expr <<<"$m"
  case "$name" in *"${1:-}"*) ;; *) continue;; esac
  rm -rf "$work/src"; mkdir -p "$work/src"
  cp -r "$root/arch" "$root/ntoskrnl" "$root/sdk" "$root/tests" "$work/src/"
  before=$(md5sum "$work/src/$file" | cut -d' ' -f1)
  perl -0pi -e "$expr" "$work/src/$file"
  after=$(md5sum "$work/src/$file" | cut -d' ' -f1)
  if [ "$before" = "$after" ]; then echo "MUTATION-NOT-APPLIED  $name"; survived=1; continue; fi
  failed=""
  for t in $tests; do
    srcs=$(cat "$work/src/tests/mm/host/test_$t.srcs")
    out="$work/t_$t"
    if ! (cd "$work/src" && gcc $cflags -o "$out" "tests/mm/host/test_$t.c" $srcs >"$work/cc.log" 2>&1); then
      echo "BUILD-ERROR  $name in $t"; survived=1; failed="x"; break
    fi
    if ! "$out" >"$work/run.log" 2>&1 || grep -q ' FAIL' "$work/run.log"; then failed="$failed $t"; fi
  done
  if [ -n "$failed" ]; then echo "KILLED    $name  by:$failed"; else echo "SURVIVED  $name"; survived=1; fi
done
exit $survived

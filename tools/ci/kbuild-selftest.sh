#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Tests the kernel build rules of the Makefile on a fixture (tools/ci/fixtures/kernel-c/): a tiny
# Multiboot2 kernel in NASM and C that switches to long mode and divides 128-bit numbers (libgcc).
# The fixture is not the kernel. Each check has an oracle outside the Makefile: ld's own error,
# readelf, nm, objdump, QEMU's exit code. Owner: A02.
# Usage: tools/ci/kbuild-selftest.sh
set -euo pipefail

cd "$(dirname "$0")/../.."
export PATH="$HOME/opt/cross/bin:$PATH"
cross=${CROSS:-x86_64-elf-}
fx=tools/ci/fixtures/kernel-c
out=build/fx
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fxargs=(BUILD="$out" KSRC_DIRS="$fx" KERNEL_LD="$fx/kernel.ld" CROSS="$cross")
fail=0
ok()  { echo "PASS  $*"; }
bad() { echo "FAIL  $*"; fail=1; }

make clean BUILD="$out" >/dev/null

# 1. Without libgcc the link must fail on the 128-bit division helper (ld says so itself).
if make "${fxargs[@]}" all >"$tmp/nolibgcc.log" 2>&1; then
    bad "link without libgcc unexpectedly succeeded"
elif grep -q "undefined reference to \`__udivti3'" "$tmp/nolibgcc.log"; then
    ok "link without libgcc fails on __udivti3"
else
    bad "link without libgcc failed for another reason"; tail -5 "$tmp/nolibgcc.log"
fi

# 2. With the no-red-zone libgcc it links; the archive used must be the no-red-zone one.
make "${fxargs[@]}" KERNEL_LIBGCC=1 all iso >"$tmp/build.log" 2>&1 || { cat "$tmp/build.log"; exit 1; }
if grep -q 'no-red-zone/libgcc.a' "$tmp/build.log"; then ok "linked no-red-zone/libgcc.a"; else bad "libgcc is not the no-red-zone one"; fi

# 3. Structure (ADR-0002): no dynamic section, no undefined symbols, Multiboot2 accepted.
readelf -d "$out/kernel.elf" 2>&1 | grep -q 'no dynamic section' && ok "no dynamic section" || bad "dynamic section present"
[ -z "$("${cross}nm" -u "$out/kernel.elf")" ] && ok "no undefined symbols" || bad "undefined symbols"
grub-file --is-x86-multiboot2 "$out/kernel.elf" && ok "grub-file accepts the kernel" || bad "grub-file rejects the kernel"
# The entry point is the one named by ENTRY() in the linker script, not one set by the Makefile.
want=$(grep -oP 'ENTRY\(\K[^)]+' "$fx/kernel.ld")
addr=$("${cross}nm" "$out/kernel.elf" | awk -v s="$want" '$3==s {print $1}')
have=$(readelf -h "$out/kernel.elf" | awk '/Entry point/ {print $4}')
if [ "$((16#$addr))" -eq "$((have))" ]; then ok "entry point is ENTRY($want)"; else bad "entry point differs from ENTRY($want)"; fi

# 4. No red-zone access in the whole image, libgcc members included.
if "${cross}objdump" -d "$out/kernel.elf" 2>/dev/null | grep -qE -- '-0x[0-9a-f]+\(%rsp\)'; then
    bad "negative %rsp offset found"; else ok "no red-zone access"; fi

# 5. Dependency files: touching a header rebuilds only the object that includes it.
sleep 1; touch sdk/include/kamos/types.h
make "${fxargs[@]}" KERNEL_LIBGCC=1 all >"$tmp/dep.log" 2>&1
if grep -q 'main.c.o' "$tmp/dep.log" && ! grep -q '^nasm' "$tmp/dep.log"; then
    ok "header change rebuilds main.c.o only"; else bad "dependency tracking"; cat "$tmp/dep.log"; fi

# 6. Boot test on both CPU models: QEMU exit 33 and a clean KTEST-END line.
for cpu in qemu64 max; do
    if make "${fxargs[@]}" KERNEL_LIBGCC=1 test QEMU_CPU="$cpu" >"$tmp/test-$cpu.log" 2>&1; then
        ok "boot test on $cpu"; else bad "boot test on $cpu"; tail -8 "$tmp/test-$cpu.log"; fi
done

# 7. A C error fails the build (-Werror).
mkdir -p "$tmp/bad"; cp "$fx"/* "$tmp/bad/"
echo 'int broken(void) { return missing_symbol; }' >> "$tmp/bad/main.c"
if make BUILD="$tmp/badbuild" KSRC_DIRS="$tmp/bad" KERNEL_LD="$tmp/bad/kernel.ld" CROSS="$cross" \
        KERNEL_LIBGCC=1 all >/dev/null 2>&1; then
    bad "build succeeded on broken C"; else ok "broken C fails the build"; fi

# 8. __DATE__ is rejected (reproducibility).
cp "$fx"/* "$tmp/bad/"
echo 'const char *when(void) { return __DATE__; }' >> "$tmp/bad/main.c"
if make BUILD="$tmp/badbuild2" KSRC_DIRS="$tmp/bad" KERNEL_LD="$tmp/bad/kernel.ld" CROSS="$cross" \
        KERNEL_LIBGCC=1 all >/dev/null 2>&1; then
    bad "__DATE__ accepted"; else ok "__DATE__ rejected"; fi

[ "$fail" -eq 0 ] && echo "kbuild-selftest: PASS" || echo "kbuild-selftest: FAIL"
exit $fail

#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Builds the x86_64-elf cross toolchain (binutils + GCC C compiler + libgcc without red zone).
# Owner: A02. Versions and checksums come from toolchain/versions.txt.
# Usage: toolchain/build-cross.sh [prefix]     (default: ~/opt/cross)
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
prefix=${1:-$HOME/opt/cross}
target=x86_64-elf
work=${KAMOS_TOOLCHAIN_WORK:-$HOME/.cache/kamos-toolchain}
jobs=${KAMOS_JOBS:-$(nproc)}   # KAMOS_JOBS=2 on hosts with little RAM

val() { grep -E "^$1=" "$here/versions.txt" | cut -d= -f2; }
binutils_v=$(val binutils); binutils_sha=$(val binutils_sha256)
gcc_v=$(val gcc);           gcc_sha=$(val gcc_sha256)
mirror=$(val gnu_mirror)

mkdir -p "$work" "$prefix"
cd "$work"

fetch() { # name url sha256
    if [ ! -f "$1" ]; then curl -fL --retry 3 -o "$1.part" "$2" && mv "$1.part" "$1"; fi
    echo "$3  $1" | sha256sum -c -
}

fetch "binutils-$binutils_v.tar.xz" "$mirror/binutils/binutils-$binutils_v.tar.xz" "$binutils_sha"
fetch "gcc-$gcc_v.tar.xz" "$mirror/gcc/gcc-$gcc_v/gcc-$gcc_v.tar.xz" "$gcc_sha"

[ -d "binutils-$binutils_v" ] || tar xf "binutils-$binutils_v.tar.xz"
[ -d "gcc-$gcc_v" ] || tar xf "gcc-$gcc_v.tar.xz"

# libgcc without red zone: add the multilib described in the OSDev wiki "Libgcc without red zone".
cfg=gcc-$gcc_v/gcc/config/i386/t-x86_64-elf
if [ ! -f "$cfg" ]; then
    printf 'MULTILIB_OPTIONS += mno-red-zone\nMULTILIB_DIRNAMES += no-red-zone\n' > "$cfg"
    sed -i '/^x86_64-\*-elf\*)/a\	tmake_file="${tmake_file} i386/t-x86_64-elf"' gcc-$gcc_v/gcc/config.gcc
fi

export PATH="$prefix/bin:$PATH"

rm -rf build-binutils && mkdir build-binutils && cd build-binutils
"../binutils-$binutils_v/configure" --target=$target --prefix="$prefix" \
    --with-sysroot --disable-nls --disable-werror
make -j"$jobs"
make install
cd ..

rm -rf build-gcc && mkdir build-gcc && cd build-gcc
"../gcc-$gcc_v/configure" --target=$target --prefix="$prefix" \
    --disable-nls --enable-languages=c --without-headers --disable-hosted-libstdcxx
make -j"$jobs" all-gcc all-target-libgcc
make install-gcc install-target-libgcc
cd ..

"$prefix/bin/$target-gcc" --version | head -n1
"$prefix/bin/$target-gcc" -print-multi-lib
echo "Cross toolchain installed in $prefix"

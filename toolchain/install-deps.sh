#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Host packages for Ubuntu 24.04 (native or WSL2). Owner: A02. Annex A.1.
set -euo pipefail

sudo apt-get update
sudo apt-get install -y \
    build-essential bison flex libgmp3-dev libmpc-dev libmpfr-dev texinfo curl xz-utils \
    nasm qemu-system-x86 ovmf xorriso mtools grub-pc-bin grub-common gdb git cmake python3 \
    clang lld llvm clang-format clang-tidy cppcheck gcc-mingw-w64-x86-64

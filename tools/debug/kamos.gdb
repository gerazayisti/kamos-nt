# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# GDB init for Kamos-NT under QEMU. Owner: A02. Use through tools/debug/gdb.sh, which passes the
# kernel image and connects ("target remote") before this file is read.
# Hardware breakpoints are used because the kernel runs before paging is on and a software
# breakpoint writes into memory that may not be mapped yet.
set pagination off
set confirm off
set print pretty on
set disassemble-next-line on
set style enabled off

define kbreak
  hbreak $arg0
end
document kbreak
kbreak LOCATION: hardware breakpoint (works before and after the switch to long mode).
end

define kmode
  show architecture
  info registers rip cs
  x/3i $pc
end
document kmode
kmode: architecture GDB uses, rip, cs, and the next three instructions at $pc.
end

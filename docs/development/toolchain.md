# Toolchain, build, QEMU and GDB

Owner: A02. State: Wave 1, in progress. Sections marked "pending" are not done yet; nothing in
this file is claimed beyond what the quoted output shows.

## Tools and versions

- Cross compiler and linker: `x86_64-elf-gcc` 16.2.0 and binutils 2.47, built by
  `toolchain/build-cross.sh`, pinned in `toolchain/versions.txt` (inputs of the CI cache key).
- Host tools observed on Ubuntu 24.04 (not part of the cache key): `toolchain/host-tools.txt`.
- Check: `make toolchain-check`.

## Kernel build rules

`make all` builds every `.c` and `.asm` file found under `boot/`, `arch/x86_64/`, `hal/` and
`ntoskrnl/` and links them with `arch/x86_64/boot/kernel.ld` into `build/kernel.elf`.

- Sources are sorted by byte value, so the link order does not depend on the file system.
- C: `KCFLAGS` plus `KOPT` (default `-O2 -g`), with `-MMD -MP` dependency files in
  `build/obj/`. NASM: `-f elf64`, `-MD -MP`. There is no `.S`.
- The entry point is the `ENTRY()` of the linker script. The Makefile does not name it.
- With no source in those directories, the placeholder in `tools/image/placeholder/` is built,
  unchanged. If sources exist but `arch/x86_64/boot/kernel.ld` does not, the build stops with an
  error.
- libgcc: not linked by default. `KERNEL_LIBGCC=1` links a copy of the `no-red-zone` multilib
  archive with its debug information stripped (`build/libgcc-nodebug.a`). The fixture shows the
  difference: without it the link fails with ``undefined reference to `__udivti3'``.
- Reproducibility: `-ffile-prefix-map` for the source tree and for the toolchain root,
  `-Wdate-time` (an error with `-Werror`), `--build-id=none`, no timestamps in the ISO.
  NASM has no prefix map, so assembly objects carry symbols but no DWARF by default. For
  line-level debugging of assembly in a private build use `make KASMFLAGS="-g -F dwarf"`; that
  image depends on the build path.
- `-n` produces one RWX segment for the fixture and `ld` warns about it. Splitting segments and
  W^X (ADR-0005) belong to A03's linker script.

Build-rule test: `tools/ci/kbuild-selftest.sh` builds `tools/ci/fixtures/kernel-c/` (a small
Multiboot2 kernel in NASM and C that switches to long mode and divides 128-bit numbers), checks
the structure with `readelf`, `nm`, `objdump` and `grub-file`, boots it on `qemu64` and `max`, and
checks that a C error and `__DATE__` fail the build. The fixture is not the kernel.

Reproducibility: `tools/ci/reproducible.sh` (placeholder) and, for the fixture,
`KAMOS_BUILD_DIR=build/fx KAMOS_MAKE_ARGS="BUILD=build/fx KSRC_DIRS=tools/ci/fixtures/kernel-c KERNEL_LD=tools/ci/fixtures/kernel-c/kernel.ld KERNEL_LIBGCC=1" tools/ci/reproducible.sh`.
It also fails if the worktree path, the other worktree path or `$HOME` appears in `kernel.elf`.
Before the path fixes this check failed on the fixture: the NASM DWARF, the compiler include
directory and the debug info of libgcc each named an absolute path.

## QEMU profiles

`tools/qemu/` has one script per profile of the plan, Annex A.5. Common options: `--cpu qemu64|max`,
`--mem`, `--timeout S`, `--headless`, `--print` (print the command line and stop).

| Profile | Script | Effect |
|---|---|---|
| normal | `normal.sh` | window, no serial capture |
| serial | `serial.sh` | headless, `build/serial.log`, `-no-reboot` |
| faults | `faults.sh` | `-d int,cpu_reset -D build/qemu.log -no-reboot -no-shutdown`, serial on stdout |
| gdb | `gdb.sh [--port N]` | QEMU stopped at reset, waiting on tcp 1234 |
| test | `run-test.sh` (`make test`) | headless, `isa-debug-exit`, exit code 33 means pass |
| disk | `disk.sh [--mport N]` | `build/disk.img` (16 MiB raw, created if missing), monitor on telnet 5555 |

Check: `tools/qemu/selftest.sh` (needs `build/kamos.iso`). It compares each command line with the
plan, then runs serial, faults, gdb (port open and guest silent while halted) and disk
(`info block` through the monitor) for both CPU models.

## GDB

Start QEMU with `tools/qemu/gdb.sh`, then `tools/debug/gdb.sh [port]` (loads `build/kernel.elf`,
connects, reads `tools/debug/kamos.gdb`). Commands: `kbreak` (hardware breakpoint), `kmode`.

### Is `set architecture i386:x86-64` needed?

Question: can GDB follow QEMU from the 32-bit protected mode that GRUB leaves to the 64-bit mode
of the kernel? Experiment: `tools/debug/arch-check.sh` boots the fixture paused with GDB 15.1
and QEMU 8.2.2, stops before `mov %eax,%cr0`, steps across the switch, and breaks in C. The
fixture has `inc %ecx` (opcode 0x41), which is a REX prefix when decoded as 64-bit code, so the
listing of the 32-bit code is compared with `objdump -M i386`. Run on 2026-10-31, branch
`agent/a02/p1-build`.

**Result: it is not needed to follow the switch, and it does not help the 32-bit listing.**

Without any command (GDB default, "auto" is already `i386:x86-64`):

```
Breakpoint 1, 0x00000000001000a5 in fixture_entry[fill] ()
The target architecture is set to "auto" (currently "i386:x86-64").
cs             0x10                16
0x00000000001000a8 in fixture_entry[fill] ()
0x00000000001000af in fixture_entry[fill] ()
0x00000000001000c0 in long_entry ()
rip            0x1000c0            0x1000c0 <long_entry>
cs             0x8                 8
Breakpoint 2, fixture_main () at tools/ci/fixtures/kernel-c/main.c:38
rip            0xffffffff801040a0  0xffffffff801040a0 <fixture_main>
#1  0xffffffff8010400e in high_entry ()
```

With `set architecture i386:x86-64` the output is the same, apart from the architecture line
(`is set to "i386:x86-64"`).

With `set architecture i386` GDB cannot attach to this QEMU:

```
warning: Selected architecture i386 is not compatible with reported target architecture i386:x86-64
warning: Architecture rejected target-supplied description
Remote 'g' packet reply is too long (expected 312 bytes, got 608 bytes): 0000...
No hardware breakpoint support in the target.
```

Limit found: while the CPU is in 32-bit protected mode GDB still decodes memory as 64-bit code.
The oracle shows it (`x/6i` at the fill loop, GDB against `objdump -M i386`):

```
GDB:      mov    %eax,0x103000(,%rcx,8)      objdump -M i386: mov %eax,0x103000(,%ecx,8)
GDB:      cmp    $0x200,%r9d                 objdump -M i386: inc %ecx   (then cmp $0x200,%ecx)
```

`inc %ecx` is swallowed as a REX prefix, so a listing of the GRUB-to-long-mode trampoline in GDB
is wrong from that instruction on. `set architecture i386` after attaching is rejected
("Architecture `i386' not recognized"), and `set disassembler-options i386` answers "not
supported on this architecture". Use `objdump -M i386 -d build/kernel.elf` to read the 32-bit
part, and GDB for the 64-bit part and for C. Register values (`cs`, `eflags`, `rip`) and
breakpoints at the 32-bit code were correct.

Other observations: at the reset vector GDB shows `0x000000000000fff0 in ?? ()` and decodes
garbage (real mode, segment base ignored). A software `break` also worked on the fixture, on
the physical entry before paging and on the high-half C function. `kamos.gdb` still uses
`hbreak` as the default for code that runs before paging.

Not yet done: the same run against A03's real trampoline (pending, A03 has no source yet), and
a run on a KVM host.

## Debug and release profiles

Pending (needs kernel C and A01's change to `bugcheck.h`).

## Format and static analysis

Pending.

## CI

The workflow is `.github/workflows/ci.yml`; logic lives in `tools/ci/`. The first remote run and
the pinning of the actions are pending; see `docs/testing/evidence.md` for the outcome once
observed.

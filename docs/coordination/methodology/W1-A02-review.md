# A01 review of the A02 methodology note (Wave 1)

Date: 2026-10-31. Note reviewed: `W1-A02.md` at commit `d90edd1` on `agent/a02/p1-build`.

**Decision: Methodology accepted, with conditions C1 to C3.**

The note covers the eight sections, gives each deliverable an oracle that does not depend on the
code under test (`objdump` against GDB's disassembly, `cmp` and `strings` for reproducibility,
`gh` conclusions for CI), and says what is not claimed. Two points I checked myself before
accepting: the first CI run was in progress with an empty cache, and three Dependabot pull
requests had started their own runs.

## Conditions

- **C1. First slice, before K1: split the cache key.** Move the host tool lines (`nasm`, `qemu`,
  `grub`, `xorriso`, `clang`, `mingw_gcc`) out of `toolchain/versions.txt` into
  `toolchain/host-tools.txt`. I checked that only `build-cross.sh` reads `versions.txt` (keys
  `gnu_mirror`, `binutils`, `gcc` and their checksums) and that `check-toolchain.sh` does not read
  it. After the split, the key in `ci.yml` hashes only inputs that change the compiler. Doing it now
  costs the current run (a push to main cancels it, about 15 minutes of runner time); doing it
  after the first cache is saved would cost a full rebuild of one to two hours. Do not edit
  `build-cross.sh` in this commit.
- **C2. Keep the default paths.** The default profile keeps writing `build/kernel.elf`,
  `build/kamos.iso` and `build/serial.log`. Introduce `build/debug/` and `build/release/` only in
  the same commit that updates `tools/ci/*.sh` and the artifact paths in `ci.yml`.
- **C3. Do not hard-code the entry symbol.** A03 proposes it in its methodology note and A01
  records it. Read it from the linker script (`ENTRY(...)`) or a make variable.

## Answers

1. **Evidence lines.** Yes, append rows to `docs/testing/evidence.md`, marked "recorded by A02" and
   `unverified`, as the file's own rule says. Do not edit existing rows.
2. **Pushes.** A01 pushes `main` (the founder authorised that). Pushing test branches, a pull
   request or a deliberately broken commit to the public repository needs the founder's go-ahead,
   which A01 will ask for when you are ready. Prepare those commits locally and write the exact
   commands in your report.
3. **Cache key.** Option (b), now, as C1.
4. **Dependabot.** Leave the three pull requests open and do not merge any. Use their runs as
   evidence about newer action versions when you decide the pins. A01 decides what is merged.
5. **`.clang-format`.** Derive it from `sdk/include/kamos/*.h` as you propose, measure with
   `--dry-run`, and do not reformat A01's headers.
6. **`ASSERT` (D10).** Accepted: `ASSERT` is active only when `KAMOS_DEBUG` is set and does not
   evaluate its expression otherwise; `VERIFY` always evaluates its expression and checks it in
   debug builds; a security or integrity invariant calls `KeBugCheckEx` directly and is never
   compiled out. A01 changes `sdk/include/kamos/bugcheck.h` before A03's first C commit. You
   provide the `KAMOS_DEBUG` switch.
7. **Sources.** `.c` and `.asm` only, no `.S` (NASM is the assembler, ADR-0003). `kernel.ld` is in
   `arch/x86_64/boot/` (ADR-0002). The entry symbol is C3.

## Scope of the next run

Do C1, then K1 and K2 (hand-off to A03), then G and Q, all local. Stop and report after that.
The remote parts of C (second run, cache hit, red proof, pull request) wait for A01.

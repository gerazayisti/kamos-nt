# Evidence log

Owner: A10. One entry per acceptance criterion: command, date, commit, observed output.
An entry recorded by another agent is marked `unverified` until A10 replays it from a
clean checkout.

| Date | Criterion | Command | Commit | Result | Verified by A10 |
|---|---|---|---|---|---|
| 2026-10-30 | `make all iso` builds placeholder kernel and ISO (host binutils 2.42, cross toolchain not built yet) | `make all iso CROSS=` | initial commit | Pass: `grub-file --is-x86-multiboot2` OK, `headers-check: OK`, `iso: build/kamos.iso` | unverified |
| 2026-10-30 | `make test` boots the ISO headless | `make test CROSS=` | initial commit | Pass: serial `Multiboot2 handoff OK`, `KTEST-END pass=1 fail=0 skip=0`, QEMU 8.2.2 exit code 33 | unverified |
| 2026-10-30 | Reproducible build | two `make clean iso CROSS=` runs | initial commit | `kernel.elf` identical (sha256 126129e2…e23f); `kamos.iso` differs (xorriso dates), open | unverified |
| 2026-10-31 | Cross toolchain built | `KAMOS_JOBS=1 toolchain/build-cross.sh` | 53479bb + uncommitted build-cross.sh | Pass: `x86_64-elf-gcc` 16.2.0, `-print-multi-lib` lists `no-red-zone;@mno-red-zone` | unverified |
| 2026-10-31 | `make toolchain-check` | `make toolchain-check` | 53479bb + uncommitted | Pass: `toolchain-check: OK`, exit 0 | unverified |
| 2026-10-31 | `make all iso test` with cross toolchain | `make all iso test CROSS=x86_64-elf-` | 53479bb + uncommitted | Pass: `Multiboot2 OK`, `headers-check: OK`, `KTEST-END pass=1 fail=0 skip=0`, QEMU exit 33 | unverified |
| 2026-10-31 | Reproducible ISO | three `make iso CROSS=x86_64-elf-` runs, 2 s apart, `cmp -l` | 53479bb + uncommitted Makefile | Pass: identical sha256 `daf4a064fa8827df…`, 0 differing bytes; fix is `--modification-date`, `-iso_nowtime =$SOURCE_DATE_EPOCH`, `-alter_date_r b =$SOURCE_DATE_EPOCH /` | unverified |
| 2026-10-31 | Replay from two clean clones at different paths, 3 s apart | `git clone` twice, `make clean all iso CROSS=x86_64-elf-` in each, `cmp` | dde257e | Pass: `kernel.elf` sha256 `126129e2be4dc292bf68…` and `kamos.iso` `75e60fba1087bd0aa1aa…` identical in both; no build path in `kernel.elf` strings | Replayed by the authoring session, not independent (see `docs/reports/p0-A10.md`) |
| 2026-10-31 | `make test` in a clean clone | `make test CROSS=x86_64-elf-` | dde257e | Pass: `KTEST-END pass=1 fail=0 skip=0`, QEMU exit 33 | Same caveat |
| 2026-10-31 | No dynamic section, no undefined symbols | `readelf -d`, `x86_64-elf-nm -u` on `kernel.elf` | dde257e | Pass: "There is no dynamic section", no undefined symbols | Same caveat |
| 2026-10-31 | Broken assembly fails the build | append an invalid instruction to `entry.asm`, `make all CROSS=x86_64-elf-` | dde257e | Pass: `make` exit 2, file restored | Same caveat. Not a CI test: no CI exists |
| 2026-10-31 | Cross GCC compiles C with the kernel flags and uses the no-red-zone libgcc | compile a small file with the ADR-0003 flags, `-mno-red-zone -print-libgcc-file-name` | dde257e | Pass: compiles, 0 `rsp-N` accesses, libgcc path ends in `no-red-zone/libgcc.a`, broken source gives gcc exit 1 | Same caveat. Linking only checked that `ld -r` succeeds |

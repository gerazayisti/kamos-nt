# Evidence log

Owner: A10. One entry per acceptance criterion: command, date, commit, observed output.
An entry recorded by another agent is marked `unverified` until A10 replays it from a
clean checkout.

| Date | Criterion | Command | Commit | Result | Verified by A10 |
|---|---|---|---|---|---|
| 2026-10-30 | `make all iso` builds placeholder kernel and ISO (host binutils 2.42, cross toolchain not built yet) | `make all iso CROSS=` | initial commit | Pass: `grub-file --is-x86-multiboot2` OK, `headers-check: OK`, `iso: build/kamos.iso` | unverified |
| 2026-10-30 | `make test` boots the ISO headless | `make test CROSS=` | initial commit | Pass: serial `Multiboot2 handoff OK`, `KTEST-END pass=1 fail=0 skip=0`, QEMU 8.2.2 exit code 33 | unverified |
| 2026-10-30 | Reproducible build | two `make clean iso CROSS=` runs | initial commit | `kernel.elf` identical (sha256 126129e2…e23f); `kamos.iso` differs (xorriso dates), open | unverified |

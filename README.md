# Kamos-NT

Kamos-NT is an independent, open-source operating system for x86-64 whose kernel
architecture is inspired by the published design of Windows NT: a hybrid kernel with an
object manager, a security reference monitor, an I/O manager and a PE image loader.
It is not Windows, contains no Microsoft code, and makes no compatibility claim beyond what
[`docs/compatibility/matrix/`](docs/compatibility/matrix/index.md) records with evidence.

**Status:** Phase 0 (architecture and tooling). Nothing boots yet except a build placeholder.
See [`docs/status/`](docs/status/README.md) and [`docs/roadmap.md`](docs/roadmap.md).

## Build

Host: Ubuntu 24.04, native or under WSL2 (repository on the Linux file system).

```
toolchain/install-deps.sh        # host packages
toolchain/build-cross.sh         # x86_64-elf binutils + GCC into ~/opt/cross (30-60 min)
make toolchain-check
make iso                         # build/kamos.iso
make test                        # headless QEMU run, PASS on exit code 33
```

## Project rules

- Architecture decisions: [`docs/adr/`](docs/adr/). Interface changes go through
  [`docs/rfc/`](docs/rfc/).
- Clean-room policy: [ADR-0012](docs/adr/0012-compatibility-and-clean-room.md).
- Contributing: [`CONTRIBUTING.md`](CONTRIBUTING.md).

## License

Apache License 2.0, see [`LICENSE`](LICENSE). Boot images built by `make iso` contain
GRUB, which is distributed under GPLv3 with its own license and source.

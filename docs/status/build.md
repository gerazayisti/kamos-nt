# Status: Build and tooling

Owner: A02

| Feature | Status | Evidence |
|---|---|---|
| Placeholder kernel (Multiboot2, COM1, isa-debug-exit) | Implemented | |
| `make iso`, `make test` | Tested (QEMU), with the cross toolchain | docs/testing/evidence.md 2026-10-31 |
| Reproducible `kernel.elf` and ISO | Tested (clean clones, different paths) | docs/testing/evidence.md 2026-10-31 |
| x86_64-elf cross toolchain (GCC 16.2.0, binutils 2.47, no-red-zone multilib) | Built | docs/testing/evidence.md 2026-10-31; compiles C with kernel flags |
| Kernel C code built by the cross GCC | Planned (placeholder is assembly only) | |
| CI (workflow, scripts, Dependabot) | Implemented. Scripts pass locally, not run on GitHub yet | docs/testing/evidence.md 2026-10-31 |

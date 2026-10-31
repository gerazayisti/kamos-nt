# Directory ownership

| Path | Owner |
|---|---|
| `README.md`, `CONTRIBUTING.md`, `LICENSE`, `docs/adr/`, `docs/rfc/`, `docs/roadmap.md`, `docs/coordination/`, `docs/architecture/overview.md`, `docs/architecture/lock-order.md`, `sdk/include/` | A01 ARCH |
| `Makefile`, `cmake/`, `toolchain/`, `.devcontainer/`, `.github/`, `tools/ci/`, `tools/image/`, `tools/qemu/`, `tools/debug/`, `tools/check-toolchain.sh`, `docs/development/` | A02 BUILD |
| `boot/`, `arch/x86_64/{boot,cpu,interrupts}/`, `hal/` | A03 BOOT |
| `ntoskrnl/rtl/` | A03 until the Wave 1 gate, then A05 |
| `ntoskrnl/mm/`, `arch/x86_64/mm/` | A04 MM |
| `ntoskrnl/ke/`, `ntoskrnl/ex/`, `arch/x86_64/context/` | A05 KE |
| `ntoskrnl/ob/`, `ntoskrnl/se/` | A06 OBSE |
| `ntoskrnl/ps/`, `ntoskrnl/lpc/`, `arch/x86_64/syscall/` | A07 PS |
| `ntoskrnl/io/`, `ntoskrnl/cm/`, `drivers/` | A08 IO |
| `lib/pe/`, `dll/ntdll/`, `tools/pe/` | A09 UM |
| `tests/framework/`, `tools/fuzz/`, `docs/testing/`, `docs/security/`, `docs/compatibility/provenance.md` | A10 QA |
| `dll/win32/` (except user32, gdi32), `tools/compat/`, `tests/conformance/` | A11 COMPAT |
| `subsystems/windowing/`, `dll/win32/user32/`, `dll/win32/gdi32/` | A09b GUI |
| `docs/status/<subsystem>.md`, `tests/<subsystem>/` | The subsystem owner |
| `docs/compatibility/matrix/` | A09 (`pe.md`, `abi.md`), A11 (`win32.md`, `chromium.md`), A01 (`index.md`) |

Additional rules (RFC-0002):

- `docs/reports/*-A10.md` belong to A10, `docs/reports/*-A02.md` to A02, and so on: a report is
  owned by the agent in its name. Gate files are A01's.
- `docs/coordination/BOARD.md`: every agent edits its own task rows. Any agent may append a defect
  row (`D<nn>`) for another agent's work, with reproduction steps. Only A01 records acceptance of a
  methodology note or of a defect fix.
- A shared include line in a file owned by another agent (for example `include tests/framework/host.mk`
  in the Makefile) is requested through the owner's inbox; the owner adds it.

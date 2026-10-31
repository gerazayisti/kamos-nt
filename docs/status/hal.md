# Status: Boot, CPU, HAL

Owner: A03

Status words: Done = built and run under QEMU (observed by A03, not yet confirmed by A10);
Open = not done. Evidence rows are in `docs/testing/evidence.md` (column "unverified" until A10 replays).

| Feature | Status | Evidence |
|---|---|---|
| Multiboot2 entry, long mode, own page tables (W^X image, null guard) | Done | `docs/architecture/boot-contract.md`, GDB walk |
| COM1 console, `kprintf`, VGA bug check screen | Done | `tests/hal/all.manifest` |
| `BOOT_INFORMATION` parser (merge rule of `boot.h`) | Done | host `test_bootinfo`, kernel suite `boot` |
| `ASSERT` / bug check path, exit 0x12 | Done | scenario `assert` (debug 37, release 33) |
| GDT, TSS, IST1 #DF, IST2 NMI, IST3 #MC | Done | suite `exc`, scenario `df` |
| IDT, 256 stubs, exception line format | Done | `docs/architecture/interrupts.md`, suites `exc`, `pf` |
| ADR-0013 page fault policy, `.kfixup` table | Done | suite `pfpolicy`, scenarios `pf-*`, host `test_fixup` |
| PIC remap, spurious IRQ 7 and 15 | Done | suite `pic` |
| PIT 100 Hz, TSC calibration (interrupt driven), monotonic clock | Done; accuracy in `docs/reports/p2-A03.md` | suite `timer`, ratio line in `all.manifest` |
| Software levels (ADR-0009): `KeGetCurrentIrql/RaiseIrql/LowerIrql`, dispatch interrupt at depth 1 | Done | suites `irql`, `dispatch`, scenarios `irql-*` |
| SMEP and SMAP enabled | Done, but no `stac`/`clac` yet (A04 user copy must add them) | CR4 0x300020 on `max` |
| RSDP search | Partial: EBDA is not scanned | `hal/bootinfo.c` |
| APIC, SMP, HPET | Open (outside Phase 2) | |

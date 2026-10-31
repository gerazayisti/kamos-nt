# Boot contract

Owner: A03. State: Implemented, `Tested (QEMU)` for what the evidence file lists. Sources:
Multiboot2 specification 2.0 (sections 3 and 3.6), Intel SDM vol. 3 (ch. 4 paging, 9.8 mode switch).

## Names

| Name | Where | Meaning |
|---|---|---|
| `KiBootEntry32` | `arch/x86_64/boot/boot.asm` | `ENTRY()` of `kernel.ld`. 32-bit code at its physical address. |
| `KiStartKernel(ULONG MultibootMagic, ULONG MbiPhysical)` | `arch/x86_64/boot/start.c` | First C function. Never returns. |

## State at `KiBootEntry32` (given by GRUB, Multiboot2 spec 3.3)

`EAX = 0x36D76289`, `EBX` = physical address of the boot information, protected mode with paging
off, `CS` a 32-bit code segment, `DS ES FS GS SS` 32-bit data segments, `A20` on, `EFLAGS.IF = 0`,
`EFLAGS.VM = 0`, `ESP` undefined. Observed in GDB on QEMU 8.2.2: `cs=0x10`, `cr0=0x11` (PE and ET),
`eax=0x36d76289`, `ebx=0x13e5a0` for a 512 MiB machine with one module.

## What the trampoline does

1. Saves `EBX`; checks the magic, CPUID long mode and NX. On failure it writes a message to COM1
   (not programmed: QEMU does not need it) and leaves through `isa-debug-exit` with `0x12`.
2. Zeroes the page tables (section `.pgtbl`, NOBITS, outside `.bss`) and builds them:

| Virtual range | Physical | Pages | Rights |
|---|---|---|---|
| `0x0` to `0xFFFF` | none | absent | null guard (ADR-0005) |
| `0x10000` to `0x1FFFFF` | identity | 4 KiB | RW, executable until `ArchFinishBootMappings` |
| `0x200000` to `0xFFFFFFFF` | identity | 2 MiB | RW, NX |
| `KERNEL_VMA + phys` for `.text` | image | 4 KiB | R X |
| for `.rodata` | image | 4 KiB | R, NX |
| for `.data`, `.pgtbl`, `.bss`, boot stack | image | 4 KiB | RW, NX |

   `KERNEL_VMA = 0xFFFFFFFF80000000`. The image must end below 2 MiB of physical memory (a link
   assertion in `kernel.ld`).
3. `CR4.PAE`, `CR3`, `EFER.LME` and `EFER.NXE`, `CR0.PG | WP | NE | PE`, loads a 3-entry GDT,
   far jump to 64-bit code, loads data selectors, jumps to the high half, sets `RSP` to the top of
   the 16 KiB boot stack, clears `RFLAGS`, clears `.bss` (not `.pgtbl`), calls `KiStartKernel`.

## State at `KiStartKernel`

`EDI` = magic (checked, so always `0x36D76289`), `ESI` = `MbiPhysical`. `CS = 0x08` (boot GDT, 64-bit
code), `RSP` 16-byte aligned plus the return address, `IF = 0`, `DF = 0`. Observed in GDB:
`cr0 = 0x80010031` (PG WP NE ET PE), `cr4 = 0x20` (PAE), `efer = 0xd00` (NXE LMA LME).
Memory below 4 GiB is reachable at its physical address (identity map) except the first 64 KiB and the
kernel image's own pages, whose identity alias `ArchFinishBootMappings` removes first thing (a
writable alias of `.text` would defeat W^X). `ArchInitCpuFeatures` sets `CR4.SMEP` and `CR4.SMAP`
when CPUID leaf 7 reports them (QEMU `max`: `cr4 = 0x300020`; `qemu64`: `0x20`).

## Boot information

`HalInitializeBootInformation` calls `HalpParseMultiboot2` (pure; `hal/bootinfo.c`). Rules, as
fixed by A01 in `boot.h` (R-c): same-type regions that overlap or touch are merged, zero-length
entries dropped, the result sorted. Bug check `KAMOS_STOP_BAD_BOOT_INFORMATION` (P1 = reason code,
`HALP_BOOTINFO_RESULT`; P2 = MBI address; P3 = magic) for: wrong magic, unaligned or oversized
structure (limit 256 KiB), a tag whose size is below 8 or runs past the end, a missing end tag, a
malformed memory map tag (entry size below 24 or not a multiple of 8, a size that is not a whole
number of entries, version not 0, two map tags), different-type overlap, `Base + Length` past 2^64,
more than 128 regions after merging, no usable region, more than 8 modules, a module without NUL or
with a name of 32 characters or more, an empty or backwards module, a module outside usable memory or
overlapping `[1 MiB, __kernel_end)`, a framebuffer whose pitch is smaller than a scanline, an ACPI
RSDP whose checksum fails. The EBDA is not scanned for the RSDP (its pointer is in the unmapped
first 64 KiB): `AcpiRsdp` is the original found in `0xE0000` to `0xFFFFF` that matches the loader's
copy, else 0.

## Console and stop lines (stable formats, A10 matches them)

```
Kamos-NT kernel 0.1.0 (debug|release) x86_64            banner, first line
ASSERT FAILED: <expression> at <file>:<line>            DbgPrintAssertFailure
KAMOS STOP 0x<8 hex> <NAME> P1=0x<hex> P2=0x<hex> P3=0x<hex> P4=0x<hex>
```

`KeBugCheckEx` prints the `KAMOS STOP` line, paints the VGA text screen (white on blue) and writes
`HAL_EXIT_BUGCHECK` (0x12, QEMU exit 37) to port `0xF4`. A bug check inside a bug check halts at once.
For a failed `ASSERT`: `P1` is the source line. Names: `ASSERTION_FAILED`, `UNHANDLED_EXCEPTION`,
`DOUBLE_FAULT`, `IRQL_VIOLATION`, `POOL_CORRUPTION`, `BAD_BOOT_INFORMATION`. Lines end with CR LF on the wire.

## Test hook

The kernel is the test kernel in v0.1. `KtMain` reads the QEMU fw_cfg file `opt/kamos/test`:
absent or `all`, the full suite; another name, one scenario that ends in a bug check (its own boot).
Without a fw_cfg device the kernel prints the banner and idles. Run:
`tests/hal/run-scenarios.sh <debug|release> <qemu64|max> [scenario ...]`.

## GDB walk to the C entry

```
tools/qemu/gdb.sh --headless --port 12345       # in one terminal, with ISO=build/debug/kamos.iso
gdb -q build/debug/kernel.elf -ex 'target remote :12345' -ex 'hbreak KiBootEntry32' \
    -ex 'hbreak KiStartKernel' -ex continue -ex 'info registers rip cs eax ebx cr0' \
    -ex continue -ex 'info registers rip cs rdi rsi rsp cr0 cr3 cr4 efer' -ex bt
```

Observed (2026-10-31, GDB 15.1, QEMU 8.2.2): first stop `KiBootEntry32` at `0x100020`, `cs=0x10`,
`eax=0x36d76289`, `cr0=0x11`; second stop `KiStartKernel` at `0xffffffff801010a3` with source line
`arch/x86_64/boot/start.c:33`, `cs=0x8`, `cr0=0x80010031`, `cr3=0x10a000`, `cr4=0x20`, `efer=0xd00`.
No `set architecture` command was needed. As A02 found, GDB decodes the 32-bit trampoline as 64-bit
code; use `objdump -M i386 -d` for that part. Hardware breakpoints (`hbreak`) are used.

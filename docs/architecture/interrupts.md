# Interrupts, exceptions and execution levels

Owner: A03. State: the line formats below are published first (A10's request); the sections after
them are written with the code, and each says what was observed.

## Exception line format (stable)

One line per exception, printed by the handler before it decides what to do:

```
EXC v=<dec> err=<0x<hex>|none> rip=0x<hex> cs=0x<hex> rflags=0x<hex> rsp=0x<hex> [cr2=0x<hex>] irql=<dec> name=<NAME> action=<resume|stop>
EXC-PF cr2=0x<hex> present=<0|1> write=<0|1> user=<0|1> rsvd=<0|1> fetch=<0|1>      (vector 14 only, right after)
```

- `v`: vector in decimal. `err`: the CPU error code in hex, or `none` for the vectors that push none
  (all but 8, 10 to 14, 17, 21, 29, 30).
- `rip`, `cs`, `rflags`, `rsp`: the frame the CPU pushed (the interrupted code). `cr2` only for
  vector 14. `irql`: the level of the interrupted code, saved by the stub (ADR-0013 uses this level, not
  the level inside the handler, which is always `DEVICE_LEVEL`).
- `name`: `DE`, `DB`, `NMI`, `BP`, `OF`, `BR`, `UD`, `NM`, `DF`, `TS`, `NP`, `SS`, `GP`, `PF`, `MF`,
  `AC`, `MC`, `XM`, `VE`, `CP`, `SX`, or `V<dec>` for an unnamed one.
- `action=resume`: a registered fixup or test expectation matched the exact `rip`; the handler set `rip`
  to the resume address. `action=stop`: bug check follows.
- Page fault error code bits (Intel SDM vol. 3, 4.7): `present` = bit 0, `write` = bit 1, `user` = bit 2,
  `rsvd` = bit 3, `fetch` = bit 4.
- A stop is followed by `KAMOS STOP 0x00000002 UNHANDLED_EXCEPTION P1=<vector> P2=<rip> P3=<error code
  or 0> P4=<cr2 or 0>` (or `0x00000003 DOUBLE_FAULT`), then exit value `0x12`.
- Numbers are lowercase hex with `0x`; addresses are printed with 16 digits.

Hardware interrupts do not print a line.

## Entry, stacks and levels

- `arch/x86_64/interrupts/stubs.asm` has 256 stubs. Each pushes a dummy error code when the CPU
  pushes none, pushes the vector, saves rax to r15 and calls `KiTrapDispatch` with the `KTRAP_FRAME`
  (22 qwords, `hal/halp.h`). All 256 gates are interrupt gates (IF cleared on entry).
- Handlers run at `DEVICE_LEVEL` with IF clear, on the interrupted stack (ring 0 to ring 0 does not
  switch stacks). The level of the interrupted code is read before it is raised and passed down;
  after the handler the level is restored to that value.
- IST: vector 8 (#DF) uses IST1, vector 2 (NMI) IST2, vector 18 (#MC) IST3, 8 KiB each, in `.bss`
  (`KiDoubleFaultStack`, `KiNmiStack`, `KiMachineCheckStack`). These three are never recovered: they
  end in a bug check. Nesting rule: an NMI during a #DF handler would reuse IST2, which is a
  different stack, so it does not overwrite the #DF frame; an NMI during an NMI handler is held by
  the CPU until `iretq`, and the NMI handler stops the machine anyway.
- Software interrupts (ADR-0009): the dispatch interrupt is a pending bit in `HALP_CPU`. It runs when
  the level is, or drops, below `DISPATCH_LEVEL`, on the current stack at depth 1 (a handler that
  requests it again is looped, never nested; `SoftMaxDepth` records the highest depth and a test
  requires 1). The handler runs with IF set. A software interrupt is also delivered at the end of a
  hardware IRQ whose interrupted level was below `DISPATCH_LEVEL`.
- `KeRaiseIrql` to `DEVICE_LEVEL` or above clears IF; `KeLowerIrql` below `DEVICE_LEVEL` sets IF unless
  a `HalDisableInterrupts` section is open. Debug builds ASSERT raise-to-lower, lower-to-higher and
  lower inside a `HalDisableInterrupts` section (scenarios `irql-raise-down`, `irql-lower-up`,
  `irql-lower-section`); release builds clamp instead.

## Fixups (ADR-0013)

- Static table: the linker section `.kfixup`, pairs of qwords `{FaultRip, ResumeRip}`, exact address
  match (`arch/x86_64/interrupts/fixup.h`). A04 emits entries from the user copy routines.
- Policy: a fixup is used only for vector 14 and only when the interrupted code ran at
  `PASSIVE_LEVEL`. On resume the handler sets `rip` to `ResumeRip`; `rflags.IF` and the level
  are those of the interrupted code. Everything else is a bug check (scenarios `pf-nofixup`,
  `pf-offbyone`, `pf-device`).
- Test registrations (`KiTestFixupRegister`, 8 slots) come in two classes: `FIXUP` behaves like the
  static table, `EXPECT` accepts any vector at any level and exists only to test the decoder.

## PIC, PIT and time

- The 8259 pair is remapped to 0x20 to 0x2F, all lines masked but the cascade. A spurious IRQ 7 or
  15 is detected with the ISR (OCW3) and gets no EOI, except that spurious IRQ 15 sends an EOI to
  the master only.
- PIT channel 0, mode 2, divisor 11932 (about 100 Hz). The timer interrupt only requests the dispatch
  interrupt (ADR-0009).
- `HalQueryMonotonicTime` is `(rdtsc - Tsc0) * Mult >> 32`, in 100 ns units. It follows the TSC, so a
  lost PIT tick cannot slow it.
- Calibration (D16): interrupt driven, no polling of the PIT counter. The timer handler reads the TSC
  as its first action; the boot code waits in `sti; hlt; cli`. 100 intervals (about 1 s). The estimate
  is the median, over all 91 windows of 10 consecutive ticks, of the TSC cost of a window. The median
  ignores the one-sided delivery jitter of TCG and a lost tick (a window with a lost tick is 10 % too
  long and is an outlier). Observed (qemu64, TCG): single intervals vary from 12004 to
  69383 kcycles around 27100; between 0 and 31 of the 91 windows were more than 2 % off the median
  in the runs seen (the boot prints the count), and the median stayed usable. An earlier method
  (least squares with lost-tick inference) mistook a late tick for two lost ticks and was dropped
  (methodology addendum).

## Other published lines

See `docs/architecture/boot-contract.md` for the banner, `ASSERT FAILED` and `KAMOS STOP`.

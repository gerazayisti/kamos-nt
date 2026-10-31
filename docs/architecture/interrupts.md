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

## Other published lines

See `docs/architecture/boot-contract.md` for the banner, `ASSERT FAILED` and `KAMOS STOP`.

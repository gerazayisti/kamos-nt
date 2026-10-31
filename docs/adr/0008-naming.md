# ADR-0008: Naming conventions

- Status: Accepted
- Date: 2026-10-30
- Owner: A01

## Decision

| Prefix | Subsystem | Owner |
|---|---|---|
| `Hal` | Hardware abstraction | A03 |
| `Ki`/`Ke` | Kernel core: traps, scheduler, synchronisation (`Ki` = internal) | A03, A05 |
| `Mm`/`Mi` | Memory manager (`Mi` = internal) | A04 |
| `Ex` | Executive support: events, mutexes, timers, pool wrappers | A05 |
| `Ob`/`Obp` | Object manager | A06 |
| `Se`/`Sep` | Security reference monitor | A06 |
| `Ps`/`Psp` | Processes and threads | A07 |
| `Lpc` | Local IPC (IPC milestone) | A07 |
| `Io`/`Iop` | I/O manager | A08 |
| `Cm`/`Cmp` | Configuration store (registry) | A08 |
| `Rtl` | Runtime library, usable in kernel and user mode | A03, then A05 |
| `Dbg` | Kernel debug output | A03 |

- Functions: `PrefixVerbObject` in PascalCase (`MmAllocatePool`, `ObReferenceObject`).
- Types: `UPPER_CASE` typedefs for structures exposed in `sdk/include/` (`KTHREAD`,
  `OBJECT_HEADER`), with a lowercase struct tag `struct _KTHREAD`.
- Macros and constants: `UPPER_CASE`. Local variables: `camelCase`.
- File names: lowercase with hyphens or underscores, prefix-free (`mm/pfn.c`).
- Names that match documented Windows APIs keep the documented spelling; internal
  structures are Kamos-NT designs even when their name is familiar.

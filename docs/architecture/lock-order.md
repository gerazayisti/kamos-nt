# Global lock order

A lock may only be acquired while holding locks of strictly **lower** rank, with one exception:
several locks of the same rank and the same kind may be held together when taken in increasing
address order (rules below). Each owner adds
its locks through a light RFC. The rank is stored in the lock when it is initialised
(`KeInitializeSpinLock`, `KeInitializeMutex`, constants `LOCK_RANK_*` in `ke.h`). Debug builds
assert the order and the execution level (ADR-0009) at every acquire.

| Rank | Lock | Type | Owner | Level when held | Protects |
|---|---|---|---|---|---|
| 10 | (reserved) process handle table | `KMUTEX` | A06 | PASSIVE_LEVEL | |
| 20 | (reserved) object namespace | `KMUTEX` | A06 | PASSIVE_LEVEL | |
| 30 | (reserved) address space, per process | `KMUTEX` | A04 | PASSIVE_LEVEL | |
| 50 | (reserved) dispatcher | spinlock | A05 | DISPATCH_LEVEL | |
| 60 | (reserved) pool | spinlock | A04 | DISPATCH_LEVEL | |
| 70 | (reserved) physical frame database | spinlock | A04 | DISPATCH_LEVEL | |
| 90 | (reserved) debug console | interrupt spinlock | A03 | DEVICE_LEVEL | |

Rules (amended 2026-10-31, RFC-0001):

- A mutex is taken at `PASSIVE_LEVEL` with no spinlock held. A spinlock may be taken while a
  mutex is held; never the reverse.
- Two locks of the same rank (for example the handle tables of two processes in
  `ObDuplicateHandle`) are taken in increasing address order.
- The panic path never takes the debug console lock. It writes to the port directly, so a
  crash while the lock is held cannot deadlock the report.

No lock is defined yet; ranks are placeholders reserved in Phase 0.

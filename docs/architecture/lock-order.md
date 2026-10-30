# Global lock order

A lock may only be acquired while holding locks that appear **above** it in this table.
Each owner adds its locks through a light RFC. Debug builds assert the order and the
execution level (ADR-0009).

| Rank | Lock | Owner | Level when held | Protects |
|---|---|---|---|---|
| 10 | (reserved) process handle table locks | A06 | PASSIVE_LEVEL | |
| 20 | (reserved) object namespace lock | A06 | PASSIVE_LEVEL | |
| 30 | (reserved) address space lock, per process | A04 | PASSIVE_LEVEL | |
| 50 | (reserved) dispatcher lock | A05 | DISPATCH_LEVEL | |
| 60 | (reserved) pool lock | A04 | DISPATCH_LEVEL | |
| 70 | (reserved) physical frame database lock | A04 | DISPATCH_LEVEL | |
| 90 | (reserved) debug console lock | A03 | HIGH_LEVEL | |

No lock is defined yet; ranks are placeholders reserved in Phase 0.

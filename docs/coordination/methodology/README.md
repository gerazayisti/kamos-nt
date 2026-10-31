# Methodology note

Before writing any code of a wave, every active agent writes
`docs/coordination/methodology/W<wave>-<ID>.md` and puts a row in `BOARD.md`. A01 reads it and
records `Methodology accepted` or `Methodology rejected: <reason>` on that row. A branch whose
first code commit predates the acceptance is not merged.

The note is short (one to two pages) and answers these questions in this order. A section that
does not apply says so and why.

1. **Goal in observable terms.** What a third party will see when the work is done: which
   command, which output, which file.
2. **Approach.** The design in a few paragraphs, with the sources used (specification chapter,
   manual section, wiki page). Only sources allowed by ADR-0012.
3. **Alternatives rejected.** At least one real alternative and the reason it lost. "None
   considered" is not an answer.
4. **Order of work.** The smallest vertical slices, each one buildable and testable alone. The
   first slice is the one that removes the largest uncertainty.
5. **Verification.** For every deliverable: the test, the layer it runs on (host, QEMU), the
   oracle (what makes it pass or fail, independent of the code under test), and the line it adds
   to `docs/testing/evidence.md`. A test without an oracle is not listed.
6. **Risks.** What can go wrong in this work (hardware quirks, concurrency, security,
   toolchain), how the work detects it early, and what the agent does if it happens.
7. **Interfaces.** What the agent needs from other agents, what it provides, and which header or
   ADR each point rests on. Any assumption not written in a header is listed as an assumption.
8. **Limits.** What this work does not do and which status label (`Planned`, `Implemented`,
   `Tested (host)`, `Tested (QEMU)`) it will claim at the end.

The note is not rewritten afterwards. A change of approach is a dated addendum at the end, with
the reason.

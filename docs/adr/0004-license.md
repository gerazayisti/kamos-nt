# ADR-0004: License

- Status: Accepted
- Date: 2026-10-30
- Owner: A01

## Decision

- Kamos-NT is open source under the **Apache License 2.0** (`LICENSE` at the root).
- Every source file starts with `SPDX-License-Identifier: Apache-2.0` and a copyright line
  `Copyright (c) <year> The Kamos-NT Authors`.
- Third-party code is accepted only under a license compatible with Apache-2.0 (MIT, BSD,
  ISC, zlib, Apache-2.0). Each import is recorded in `docs/compatibility/provenance.md`
  with origin URL, version, license and reviewer (A10).
- GPL-only code is not imported. Tools under GPL (GCC, binutils, GRUB, QEMU) are used, not
  linked into Kamos-NT artifacts. GRUB images on the ISO are distributed with their license
  and a source offer (ADR-0001).
- The project is public from the start (founder decision, 2026-10-30). Publishing to a
  remote host happens only on the founder's explicit go-ahead.

## Open point

A lawyer should review the license and trademark choices before any commercial use or
pre-installed distribution. Nothing in this ADR is legal advice.

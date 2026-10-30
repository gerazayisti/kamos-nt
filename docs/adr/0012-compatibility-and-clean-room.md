# ADR-0012: Compatibility and clean-room policy

- Status: Accepted
- Date: 2026-10-30
- Owner: A01, enforced by A10

## Allowed sources

- Public documentation: PE/COFF specification, Microsoft Learn, published SDK headers,
  Intel SDM, AMD APM, ACPI and UEFI specifications, OSDev wiki, published books.
- Code under a license compatible with Apache-2.0, with its provenance recorded (ADR-0004).
- Black-box behaviour of a licensed Windows 11 VM running **our own** test programs.
- Import tables of open-source applications (e.g. Chromium built from source), read with
  REA or `llvm-objdump`, to rank which APIs to implement.

## Forbidden sources

- Leaked or illegitimately obtained Windows source code.
- Structures or algorithms recovered by decompiling or disassembling Microsoft binaries.
  REA and Ghidra are never pointed at a Microsoft binary or at Google Chrome.
- Code copied from ReactOS, Wine or any project under an incompatible license.

## Compatibility claims

- Binary, API, ABI and driver compatibility are four separate goals.
- No compatibility claim outside `docs/compatibility/matrix/`, and every matrix row links to
  an evidence entry.
- Windows drivers are out of scope. The product name never contains "Windows".

## Procedure

An agent in doubt about a source stops and opens an RFC. A10 audits provenance before each
gate.

# ADR-0006: System call ABI

- Status: Accepted (provisional until the Wave 3 gate)
- Date: 2026-10-30
- Owner: A01 and A07

## Decision

- Entry by `syscall`; return by `sysretq`, or `iretq` when RCX is not canonical or the
  thread state was changed by the kernel. `EFER.SCE`, `STAR`, `LSTAR`, `SFMASK` (IF, TF, DF,
  AC cleared) are set by A07.
- Register contract at the `syscall` instruction:

| Register | Content |
|---|---|
| `rax` | Kamos-NT service number (32 bits, upper bits must be zero) |
| `r10` | Argument 1 (copied from `rcx` by the user stub) |
| `rdx`, `r8`, `r9` | Arguments 2–4 |
| `[rsp+0x28]` and up | Arguments 5 and up, read once into kernel memory |
| `rcx`, `r11` | Clobbered by the CPU (return RIP, RFLAGS) |
| `rax` (return) | `NTSTATUS` (ADR-0007) |

- User stubs in `ntdll` have exactly this shape, required by the Chromium sandbox
  (bytes `4C 8B D1 B8 <n> 0F 05 C3`, checked 2026-10-30 against
  `sandbox/win/src/service_resolver_64.cc`):

```
mov r10, rcx
mov eax, <Kamos service number>
syscall
ret
```

- Service numbers belong to Kamos-NT. They are generated from `sdk/syscalls/table.def` and
  never copied from any Windows release.
- The kernel validates every user pointer and copies arguments into kernel memory once
  before use (no double fetch).

## Consequences

The kernel side uses the System V ABI; translation from the Microsoft x64 convention happens
only in the entry stub. The table format is owned by A07; numbering changes are RFCs.

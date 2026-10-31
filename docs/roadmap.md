# Roadmap

No dates. A version ships when its acceptance criteria are observed and logged by A10.

## Versions

| Version | Content | Phases | Evidence |
|---|---|---|---|
| v0.1 Minimal kernel | Boot, exceptions, memory, kernel threads, objects and handles, one user program with system calls | 0–6 | ISO boots under QEMU, serial log, automated tests, user program runs and exits |
| v0.2 Executable system | Security, I/O manager, basic drivers, simple file system, PE loader, minimal ntdll | 7–10 | A PE32+ test program built by us loads and runs; malformed PEs are rejected |
| v0.3 User environment | Initial process, console, SMP, UEFI boot | 11 + milestones | Two user processes, interactive console, boot on one real machine |
| Later | Win32 GUI, wider Win32, paging file, M-HW, M-LINUX | 12+ | No public claim before the compatibility matrix supports it |

## Dependency graph

```
P0 Architecture, tooling
 └─ P1 Boot ─ P2 CPU, HAL, interrupts ─ P3 Memory ─ P4 Sync, scheduler
                                                     └─ P5 Objects, handles
                                                         └─ P6 Processes, syscalls ─ P7 Security
                                                             └─ P8 I/O, drivers ─ P9 Storage, FS, registry
                                                                 └─ P10 PE loader, ntdll ─ P11 Session, console ─ P12 Win32 GUI
Cross-cutting: build (from P0), QEMU tests (from P1), M-SMP (after v0.1)
Host-only, no kernel needed: PE parser (A09), memory logic (A04), object and security logic (A06)
```

## v0.1 acceptance criteria

1. `make test` boots the ISO headless and exits with code 33 on `qemu64` and `max` CPUs.
2. Exceptions #DE, #UD, #GP, #PF are decoded and logged; a deliberate page fault is handled.
3. Timer measured within 1 % over 10 s.
4. Map/unmap tests read back the real page tables; kernel write to an NX page is refused.
5. 10 000 thread create/destroy cycles without leaks.
6. A closed or forged handle is rejected; objects are destroyed exactly once.
7. A user program from the initrd starts, makes system calls and exits; a kernel pointer
   passed as an argument is rejected.

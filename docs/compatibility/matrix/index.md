# Compatibility matrix

Binary, API, ABI and driver compatibility are separate goals (ADR-0012).
Every row links to an evidence entry; a row without evidence stays `Planned`.

| Level | Meaning | Status |
|---|---|---|
| C0 | Malformed PE images are rejected safely | Planned |
| C1 | A console PE32+ built by us runs and returns 0 | Planned |
| C2 | Console programs using the common kernel32 subset | Planned |
| C3 | Multi-threaded console programs, files, sockets | Planned |
| C4 | Chromium `--headless` starts | Planned |
| C5 | Chromium with a window | Planned |
| C6 | Mainstream desktop applications | Planned |

Windows drivers: Unsupported (out of scope).

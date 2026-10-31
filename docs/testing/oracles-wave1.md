# Wave 1 oracles and test formats

Owner: A10. Date: 2026-10-31. Basis: ADR-0009, ADR-0010, ADR-0013 and RFC-0002 (exit values,
IF rule, page fault policy, time from TSC), the methodology note `W1-A10.md`, A01's review of it.

Status: **Designed**. Nothing in this file has been run against an A03 kernel, because none exists.
Each criterion below has a procedure, an oracle that does not come from the code under test, a
negative control, and the evidence line it will add. A criterion whose oracle cannot be built is
marked `Open`. Evidence from this profile is at most `Tested (QEMU)`.

## 1. Formats (A03 and A04 code against these)

### 1.1 Serial lines

Grammar, one line each, LF or CRLF, no other `KTEST` text anywhere:

```
KTEST <suite> <name> PASS|FAIL|SKIP [detail]
KTEST-END pass=<n> fail=<n> skip=<n>
```

- `suite` and `name` match `[A-Za-z0-9_.-]+`. A pair appears once per boot.
- `detail` is free text on the same line, with no newline. Numeric facts that the runner uses are
  written as `key=value` words (for example `t100ns=123456789`).
- `KTEST-END` appears once, last. Its counts equal the number of lines of each kind.
- A line must not be interleaved with other output. Test code and `kprintf` run on one CPU; a test
  prints a whole line through one call to the console write function.
- A boot that ends in a bug check (exit value `0x12`) has no `KTEST-END`.

### 1.2 Exit values (ADR-0010, `hal.h`)

`0x10` (QEMU 33) tests passed; `0x11` (35) at least one failed; `0x12` (37) bug check or failed
`ASSERT`. QEMU exit 1 or any other value means the kernel did not finish; 124 is the runner's
timeout. The runner checks that the exit value and the lines agree: exit 33 with a `FAIL` line, exit
35 without one, exit 33 without `KTEST-END`, exit 37 with `KTEST-END`, are all failures.

### 1.3 Manifest (`tests/framework/expected/<profile>.manifest`)

Written by hand, independent of the build. One directive per line, `#` starts a comment.

| Directive | Meaning |
|---|---|
| `exit <33\|35\|37>` | Exit code this boot must produce (default 33) |
| `require <suite> <name> [PASS\|SKIP]` | The test must appear with that result (default PASS) |
| `line <regex>` | Some serial line must match |
| `forbid <regex>` | No serial line may match |
| `ratio <suite> <start> <end> <key> <scale> <min> <max>` | The value of `key` in the details of two lines, times `scale` seconds, divided by the host interval between their arrival, must lie in `[min, max]` |
| `allow-empty` | Zero test lines accepted (only for a boot test) |

A `SKIP` that is not declared by `require ... SKIP` is a failure. A test missing from the log is a
failure. A boot that expects a bug check (`exit 37`) declares the serial line it expects with
`line`.

### 1.4 Host test layout (A04 and everyone)

- One executable per file `tests/<subsystem>/host/test_<name>.c`.
- Extra sources under test: `tests/<subsystem>/host/test_<name>.srcs`, one repository-relative path
  per line.
- Test code includes `<ktest.h>` (`tests/framework/include`) and `<kamos/...>` headers
  (`sdk/include`). It defines `int main(void)` that builds a table of cases and calls `kt_run`.
- Built with gcc and clang, `-std=c11 -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer
  -fsanitize=address,undefined -fno-sanitize-recover=all`. A sanitizer report ends the program with
  a non-zero status and the test is a failure. Run: `make test-host` once A02 adds the include of
  `tests/framework/host.mk`; until then `tests/framework/host/run-host-tests.sh`.
- Page-table tests compute the expected entries with `tests/framework/include/x86_oracle.h`
  (bit layout from the Intel SDM, written without A04's macros).

## 2. Common QEMU profile

`qemu-system-x86_64 -machine q35 -cpu qemu64|max -m 512M -display none -no-reboot -accel tcg
-serial <pipe> -monitor none -device isa-debug-exit,iobase=0xf4,iosize=0x04`. `-accel tcg` is
explicit because `/dev/kvm` exists on the development machine and a KVM guest has a different PIT and
no `-d int`. `-icount` is forbidden: it decouples guest time from the host clock. The runner is
`tests/framework/ktest-run.py` (stamps each arriving line with the host monotonic clock).

## 3. Phase 1 and 2 criteria

| # | Criterion | Procedure | Oracle (not from the code under test) | Negative control | Evidence line |
|---|---|---|---|---|---|
| O1 | Boots on `qemu64` and `max`, exit 33 | Runner with the profile's manifest | Exit 33, every required test `PASS`, END counts equal the lines | Fixture: zero tests with END `fail=0` is rejected (the placeholder's fixed line is rejected this way) | `boot qemu64/max: exit 33, N required tests, 0 problems` |
| O2 | Entry reached, banner | Manifest `line` on the banner and on the `boot entry` test | A03's published banner text; the test name list | `forbid` on `panic` | same |
| O3 | Deliberate `ASSERT` | One boot whose test fails an `ASSERT` | Exit 37, the `line` that `DbgPrintAssertFailure` prints (A03 publishes the format), no `KTEST-END` | A boot with a passing `ASSERT` must give 33 | `assert: exit 37 and line matched` |
| O4 | Timer within 1 % over 10 s | Boot test `timer` waits in an `hlt` loop until `HalQueryMonotonicTime` has advanced 10 s, printing `KTEST timer start PASS t100ns=<v>` and `KTEST timer end PASS t100ns=<v>` | Host monotonic clock between the arrival of the two lines. Manifest: `ratio timer start end t100ns 1e-7 0.99 1.01`. Cross-check in the guest: ten CMOS RTC second edges (QEMU `-rtc clock=host`, to be confirmed by comparing the edges with the host stamps). The clock under test is derived from the TSC calibrated on the PIT (RFC-0002), so a wrong calibration shows | The ratio logic is exercised on fixtures at 0.98 and 1.02 and must fail. The measured noise of the method is published in section 5 before the threshold is used | `timer: ratio r1 r2 r3 per CPU, noise floor n` |
| O5 | #DE, #UD, #GP, #PF decoded | Each fault is raised from a labelled assembly snippet (`div` by zero written in asm so that the compiler cannot fold it; `ud2`; a load of an invalid selector; an access to an unmapped address). A03 publishes the exact line format in `docs/architecture/interrupts.md` before coding | Per event: `rip` from `nm` (label + offset); vector number and presence of an error code from the Intel SDM exception table; `cr2` from the address the test used; and QEMU's own `-d int -D file` log of the same event (vector, error code, CR2; its text format is read from the first real run, not assumed). A non-canonical access must give #GP(0), not #PF | A test with a deliberately wrong expected vector or address must be reported as `FAIL` | `exceptions: 4 vectors, fields matched to nm and QEMU log` |
| O6 | #PF error code | Cases: not-present read, not-present write, kernel access to the null guard, fetch from a non-executable page, write to a read-only page | SDM page-fault error-code bits (P, W/R, U/S, RSVD, I/D) | As O5. A case that cannot be built in Wave 1 (needs A04's page tables) is declared `require ... SKIP` with the reason | `pf bits: n cases, m skipped (listed)` |
| O7 | Page fault policy (ADR-0013) | (a) Fault inside a registered test fixup: the test's counter is 1, registers set before the fault are intact, no stop code. (b) Fault outside any fixup: exit 37, decoded line. (c) Fault at `DEVICE_LEVEL` or above: exit 37. The fixup matches an exact address, never a range | Values the test set itself; exit value; serial line | Case (a) without registering the fixup must end as (b); a fixup address one byte off must not resume | `pf policy: a/b/c observed` |
| O8 | Level masking | (1) `pushfq` at each level: IF is 1 at PASSIVE and DISPATCH, 0 at DEVICE and HIGH, and 0 inside a `HalDisableInterrupts` section. (2) Raise to DEVICE; spin until the PIT counter, read by raw port access, has wrapped twice (bounded, else `FAIL`). The tick count is unchanged and the 8259 request register (OCW3 read) shows IRQ0 pending. (3) Lower to PASSIVE: the tick count increases within a bounded number of PIT reads. (4) Raising down or lowering up asserts (exit 37). (5) Interrupts off through `HalDisableInterrupts`, then raise and lower: IF is still 0 only inside the section, and `KeLowerIrql` inside it asserts | Hardware state (8259 IRR, RFLAGS, PIT latch), not A03's counters | The same spin at PASSIVE must show the tick count increasing; otherwise the timer is dead and the result is void | `masking: 5 observations, control ok` |
| O9 | Reproducible build | `tools/ci/reproducible.sh` and A10's variation (TZ, `LC_ALL`, umask, path length) | `cmp` of two independent builds. Determinism is not correctness; the evidence line says so | A copy with `__TIME__` inserted must differ | `repro: identical under variation` |
| O10 | Boot information validation (threat model B1) | Needs a way to feed a malformed structure | Open: A03 is asked for a function that validates a structure at a given address, so a host test can feed it overlapping, truncated and oversized maps. Without it B1 stays `Designed` | | |
| O11 | Fixup table logic | Host test of the table (exact-address match) once A03 documents the format | Test-set values | Entry removed, must fail | `fixup: host cases` |

## 4. Known limits

The emulated PIT counts virtual time exactly, so O4 can detect a wrong calibration, divisor or unit
conversion, and a lost-interrupt bug, but not crystal drift. It proves nothing about real hardware.
QEMU's serial port is instantaneous and will not show the lost ticks that a slow console can cause on
a real machine (ADR-0009, RFC-0002 item 8); that risk stays untested.

## 5. Timer noise measurement (measured 2026-10-31)

Method: `tests/framework/probes/timer-noise/` is a 512-byte real-mode boot sector (not a kernel). It
programs PIT channel 0 at 100 Hz (divisor 11932), counts 1000 IRQ0 interrupts while idling in `hlt`,
and prints `KTEST timer start` and `KTEST timer end` with the guest's nominal 0 and 10 s. The runner
stamps the arrival of both lines with the host `CLOCK_MONOTONIC` and prints guest/host. QEMU 8.2.2,
TCG, `-machine pc -nic none`, WSL2 with 2 cores, nothing else heavy running. The host clock itself
was checked: a 10 s `sleep` gave `monotonic` 10.000342 s and `time()` 10.000343 s, and a Windows
stopwatch around the call 10.53 s (0.26 s of which is process start).

| Runs | CPU | min | max | mean |
|---|---|---|---|---|
| 20 | qemu64 | 0.993204 | 1.000039 | 0.999171 |
| 10 | max | 0.992104 | 1.000056 | 0.999216 |

Reading: 30 of 30 runs lie inside 0.99 to 1.01. The noise is **one-sided**: the guest never runs
fast, and the host interval is occasionally longer by up to about 80 ms (worst ratio 0.9921),
presumably because the end line reaches the runner late. The margin to the 1 % limit is small (0.79
points used on the worst run), and a loaded host could push a correct kernel over it. The criterion
stays at 1 %: it is not widened. If a correct kernel fails it, check the host load first; the runner
prints both intervals so that this can be seen. The method has not been run against an A03 kernel
because none exists, so this is the noise of the method, not a result for the kernel.

**Finding: do not busy-poll the PIT.** The first version of the probe polled the latched PIT count in
a tight loop and counted wraps. It gave ratios of 0.55 to 0.82 (20 runs, mean 0.75), and a version
that summed count deltas instead of wraps (so it could not miss a wrap) gave 0.79 to 0.82 in 5 runs:
under QEMU TCG the PIT time of a busy-polling guest advanced about 20 % slower than the host clock.
The same PIT with an interrupt handler and `hlt` tracks the host clock. Consequences: (1) the O4 test
waits in `hlt`, never in a polling loop; (2) the O8 spin that reads the PIT latch is bounded in guest
PIT time and does not compare it with the host, so it is not affected; (3) A03's TSC calibration
against the PIT (RFC-0002 item 8) is likely to be done by polling. If the TSC and the PIT follow the
same virtual clock the calibration is self-consistent and O4 passes; if they do not, O4 will show an
error of about 20 %. That is a design risk for A03 to know about, not a defect. I do not know why
the polling guest lags; I did not investigate QEMU's clock code.

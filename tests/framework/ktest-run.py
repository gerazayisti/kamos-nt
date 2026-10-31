#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
"""Kamos-NT kernel test runner and serial log judge. Owner: A10.

Protocol and manifest syntax: docs/testing/oracles-wave1.md, section 1.

    ktest-run.py parse  LOG --exit N [--manifest FILE] [--stamped]
    ktest-run.py qemu   --iso ISO [--cpu qemu64] [--mem 512M] [--timeout 30]
                        [--manifest FILE] [--log FILE] [--extra=ARG ...]
    ktest-run.py selftest FIXTURE_DIR

Exit status: 0 verdict PASS, 1 verdict FAIL, 2 usage or I/O error.
Python 3 standard library only.
"""
import argparse
import os
import re
import select
import subprocess
import sys
import time

NAME = r"[A-Za-z0-9_.-]+"
LINE_RE = re.compile(r"^KTEST (%s) (%s) (PASS|FAIL|SKIP)(?: (.*))?$" % (NAME, NAME))
END_RE = re.compile(r"^KTEST-END pass=(\d+) fail=(\d+) skip=(\d+)$")
EXIT_NAMES = {33: "tests passed", 35: "tests failed", 37: "bug check"}


class Manifest:
    def __init__(self):
        self.exit = 33
        self.require = []        # (suite, name, result)
        self.lines = []          # regex
        self.forbid = []         # regex
        self.ratio = []          # (suite, start, end, key, scale, lo, hi)
        self.allow_empty = False


def load_manifest(path):
    m = Manifest()
    if path is None:
        return m
    with open(path, encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            s = raw.split("#", 1)[0].strip()
            if not s:
                continue
            w = s.split(None, 1)
            d, rest = w[0], (w[1] if len(w) > 1 else "")
            if d == "exit":
                m.exit = int(rest)
            elif d == "require":
                p = rest.split()
                if len(p) not in (2, 3):
                    raise ValueError("%s:%d: require needs suite name [RESULT]" % (path, n))
                m.require.append((p[0], p[1], p[2] if len(p) == 3 else "PASS"))
            elif d == "line":
                m.lines.append(re.compile(rest))
            elif d == "forbid":
                m.forbid.append(re.compile(rest))
            elif d == "ratio":
                p = rest.split()
                if len(p) != 7:
                    raise ValueError("%s:%d: ratio needs 7 fields" % (path, n))
                m.ratio.append((p[0], p[1], p[2], p[3], float(p[4]), float(p[5]), float(p[6])))
            elif d == "allow-empty":
                m.allow_empty = True
            else:
                raise ValueError("%s:%d: unknown directive %r" % (path, n, d))
    return m


def judge(records, exit_code, manifest):
    """records: list of (host_ns or None, text). Returns a list of problems (empty = PASS)."""
    problems = []
    tests = {}            # (suite, name) -> (result, detail, stamp)
    counts = {"PASS": 0, "FAIL": 0, "SKIP": 0}
    end = None
    end_index = None
    texts = []
    for i, (stamp, raw) in enumerate(records):
        text = raw.rstrip("\r\n")
        texts.append(text)
        if text.startswith("KTEST-END"):
            m = END_RE.match(text)
            if not m:
                problems.append("malformed KTEST-END line: %r" % text)
            elif end is not None:
                problems.append("second KTEST-END line")
            else:
                end = tuple(int(x) for x in m.groups())
                end_index = i
            continue
        if text.startswith("KTEST"):
            m = LINE_RE.match(text)
            if not m:
                problems.append("malformed KTEST line: %r" % text)
                continue
            if end is not None:
                problems.append("KTEST line after KTEST-END: %r" % text)
            key = (m.group(1), m.group(2))
            if key in tests:
                problems.append("duplicate test %s %s" % key)
            tests[key] = (m.group(3), m.group(4) or "", stamp)
            counts[m.group(3)] += 1
        elif "KTEST" in text:
            problems.append("KTEST token not at the start of a line (interleaved output?): %r" % text)

    # END against the lines.
    if end is not None:
        if (counts["PASS"], counts["FAIL"], counts["SKIP"]) != end:
            problems.append("KTEST-END says pass=%d fail=%d skip=%d but the lines give pass=%d fail=%d skip=%d"
                            % (end + (counts["PASS"], counts["FAIL"], counts["SKIP"])))

    # Exit value against the content.
    if exit_code != manifest.exit:
        problems.append("exit code %s, the manifest expects %s (%s)"
                        % (exit_code, manifest.exit, EXIT_NAMES.get(manifest.exit, "?")))
    if manifest.exit == 33:
        if end is None:
            problems.append("exit 33 but no KTEST-END line")
        if counts["FAIL"]:
            problems.append("exit 33 but %d FAIL line(s)" % counts["FAIL"])
        if not tests and not manifest.allow_empty:
            problems.append("no test ran (a KTEST-END alone is not a test)")
    elif manifest.exit == 35:
        if end is None:
            problems.append("exit 35 but no KTEST-END line")
        if not counts["FAIL"]:
            problems.append("exit 35 but no FAIL line")
    elif manifest.exit == 37:
        if end is not None:
            problems.append("exit 37 (bug check) but a KTEST-END line is present")

    # Manifest requirements.
    declared_skip = set()
    for suite, name, result in manifest.require:
        got = tests.get((suite, name))
        if got is None:
            problems.append("required test %s %s is missing" % (suite, name))
        elif got[0] != result:
            problems.append("test %s %s is %s, expected %s" % (suite, name, got[0], result))
        if result == "SKIP":
            declared_skip.add((suite, name))
    for key, (res, detail, _) in tests.items():
        if res == "SKIP" and key not in declared_skip:
            problems.append("undeclared SKIP: %s %s (%s)" % (key + (detail,)))
    for rx in manifest.lines:
        if not any(rx.search(t) for t in texts):
            problems.append("no serial line matches /%s/" % rx.pattern)
    for rx in manifest.forbid:
        for t in texts:
            if rx.search(t):
                problems.append("forbidden line /%s/: %r" % (rx.pattern, t))
                break

    # Host-clock ratios.
    for suite, a, b, key, scale, lo, hi in manifest.ratio:
        ta, tb = tests.get((suite, a)), tests.get((suite, b))
        if ta is None or tb is None:
            problems.append("ratio: test %s %s or %s missing" % (suite, a, b))
            continue
        if ta[2] is None or tb[2] is None:
            problems.append("ratio: no host timestamps (run through the qemu command or --stamped)")
            continue
        va, vb = _kv(ta[1], key), _kv(tb[1], key)
        if va is None or vb is None:
            problems.append("ratio: key %s not found in the details" % key)
            continue
        host_s = (tb[2] - ta[2]) / 1e9
        guest_s = (vb - va) * scale
        if host_s <= 0:
            problems.append("ratio: non-positive host interval")
            continue
        r = guest_s / host_s
        print("ratio %s %s->%s: guest %.6f s, host %.6f s, ratio %.6f (allowed %g..%g)"
              % (suite, a, b, guest_s, host_s, r, lo, hi))
        if not (lo <= r <= hi):
            problems.append("ratio %.6f outside [%g, %g]" % (r, lo, hi))
    return problems


def _kv(detail, key):
    m = re.search(r"(?:^|\s)%s=(\d+)(?:\s|$)" % re.escape(key), detail)
    return int(m.group(1)) if m else None


def read_log(path, stamped):
    recs = []
    with open(path, "rb") as f:
        for raw in f.read().decode("utf-8", "replace").split("\n"):
            if raw == "":
                continue
            if stamped:
                ns, _, text = raw.partition("\t")
                recs.append((int(ns), text))
            else:
                recs.append((None, raw))
    return recs


def verdict(problems, label):
    if problems:
        for p in problems:
            print("PROBLEM: " + p)
        print("VERDICT FAIL (%s): %d problem(s)" % (label, len(problems)))
        return 1
    print("VERDICT PASS (%s)" % label)
    return 0


def cmd_parse(a):
    m = load_manifest(a.manifest)
    return verdict(judge(read_log(a.log, a.stamped), a.exit, m), a.log)


def cmd_qemu(a):
    m = load_manifest(a.manifest)
    cmd = ["qemu-system-x86_64", "-machine", a.machine, "-cpu", a.cpu, "-m", a.mem,
           "-display", "none", "-no-reboot", "-accel", "tcg",
           "-serial", "stdio", "-monitor", "none",
           "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"] + (a.extra or [])
    if a.disk:
        cmd += ["-drive", "format=raw,file=" + a.disk]
    elif a.iso:
        cmd += ["-cdrom", a.iso]
    else:
        print("error: --iso or --disk is required", file=sys.stderr)
        return 2
    print("+ " + " ".join(cmd))
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stdin=subprocess.DEVNULL)
    fd = p.stdout.fileno()
    t0 = time.monotonic()
    buf = b""
    recs = []
    timed_out = False
    while True:
        left = a.timeout - (time.monotonic() - t0)
        if left <= 0:
            timed_out = True
            break
        r, _, _ = select.select([fd], [], [], min(left, 0.5))
        if r:
            chunk = os.read(fd, 4096)
            now = time.monotonic_ns()
            if not chunk:
                break
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                recs.append((now, line.decode("utf-8", "replace")))
        elif p.poll() is not None:
            break
    if buf:
        recs.append((time.monotonic_ns(), buf.decode("utf-8", "replace")))
    if timed_out:
        p.kill()
        code = 124
    else:
        code = p.wait()
    p.wait()
    if a.log:
        with open(a.log, "w", encoding="utf-8") as f:
            for _, t in recs:
                f.write(t.rstrip("\r") + "\n")
        with open(a.log + ".stamped", "w", encoding="utf-8") as f:
            for ns, t in recs:
                f.write("%d\t%s\n" % (ns, t.rstrip("\r")))
    for _, t in recs:
        print("| " + t.rstrip("\r"))
    print("qemu exit code: %d%s" % (code, " (timeout)" if timed_out else ""))
    return verdict(judge(recs, code, m), a.iso or a.disk)


def cmd_selftest(a):
    """Each fixture NAME.serial has NAME.meta: exit=N, verdict=PASS|FAIL, optional manifest=FILE,
    stamped=1, reason=SUBSTRING (a bad fixture must fail for that reason). The expected verdicts are written by hand."""
    ok = bad = 0
    for fn in sorted(os.listdir(a.dir)):
        if not fn.endswith(".serial"):
            continue
        base = os.path.join(a.dir, fn[:-7])
        meta = {}
        with open(base + ".meta", encoding="utf-8") as f:
            for raw in f:
                raw = raw.split("#", 1)[0].strip()
                if raw:
                    k, _, v = raw.partition("=")
                    meta[k.strip()] = v.strip()
        mpath = os.path.join(a.dir, meta["manifest"]) if "manifest" in meta else None
        m = load_manifest(mpath)
        import io
        saved, sys.stdout = sys.stdout, io.StringIO()
        try:
            probs = judge(read_log(base + ".serial", meta.get("stamped") == "1"),
                          int(meta["exit"]), m)
        finally:
            sys.stdout = saved
        got = "FAIL" if probs else "PASS"
        want = meta["verdict"]
        if got == want and "reason" in meta and not any(meta["reason"] in x for x in probs):
            bad += 1
            print("WRONG %-34s failed for another reason than %r: %s" % (fn[:-7], meta["reason"], probs))
            continue
        if got == want:
            ok += 1
            print("ok    %-34s verdict %s" % (fn[:-7], got))
        else:
            bad += 1
            print("WRONG %-34s expected %s, got %s: %s" % (fn[:-7], want, got, probs))
    print("selftest: %d fixtures as expected, %d wrong" % (ok, bad))
    return 1 if bad or ok == 0 else 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("parse")
    p.add_argument("log")
    p.add_argument("--exit", type=int, required=True)
    p.add_argument("--manifest")
    p.add_argument("--stamped", action="store_true")
    p.set_defaults(fn=cmd_parse)
    q = sub.add_parser("qemu")
    q.add_argument("--iso")
    q.add_argument("--disk", help="raw disk image booted instead of an ISO (probes)")
    q.add_argument("--cpu", default="qemu64")
    q.add_argument("--machine", default="q35")
    q.add_argument("--mem", default="512M")
    q.add_argument("--timeout", type=float, default=30)
    q.add_argument("--manifest")
    q.add_argument("--log")
    q.add_argument("--extra", action="append", help="one extra QEMU argument, repeatable: --extra=-nic --extra=none")
    q.set_defaults(fn=cmd_qemu)
    s = sub.add_parser("selftest")
    s.add_argument("dir")
    s.set_defaults(fn=cmd_selftest)
    a = ap.parse_args(argv)
    try:
        return a.fn(a)
    except (OSError, ValueError, KeyError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

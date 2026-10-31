#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Checks the QEMU profiles on the built ISO (default build/kamos.iso) for qemu64 and max. The
# command line of each profile is compared with Annex A.5 of the plan, and each profile is run
# for its observable effect. The test profile is run-test.sh (make test). Owner: A02.
# Usage: tools/qemu/selftest.sh
set -uo pipefail

cd "$(dirname "$0")/../.."
here=tools/qemu
BUILD=${BUILD:-build}
fail=0
ok()  { echo "PASS  $*"; }
bad() { echo "FAIL  $*"; fail=1; }
has() { grep -qF -- "$2" <<<"$1"; }
[ -f "${ISO:-$BUILD/kamos.iso}" ] || { echo "build the ISO first (make iso)"; exit 2; }

wait_port() { # port, seconds
    local i
    for ((i = 0; i < $2 * 10; i++)); do
        (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0
        sleep 0.1
    done
    return 1
}

for cpu in qemu64 max; do
    echo "--- cpu $cpu"
    # 1. Command lines against Annex A.5.
    c=$($here/normal.sh --print --cpu "$cpu")
    has "$c" "-machine q35 -cpu $cpu -m 512M -cdrom build/kamos.iso" && ok "normal: command line" || bad "normal: $c"
    c=$($here/serial.sh --print --cpu "$cpu")
    has "$c" "-display none -serial file:build/serial.log -no-reboot" && ok "serial: command line" || bad "serial: $c"
    c=$($here/faults.sh --print --cpu "$cpu")
    has "$c" "-serial stdio -d int,cpu_reset -D build/qemu.log -no-reboot -no-shutdown" && ok "faults: command line" || bad "faults: $c"
    c=$($here/gdb.sh --print --cpu "$cpu")
    has "$c" "-serial stdio -gdb tcp::1234 -S" && ok "gdb: command line" || bad "gdb: $c"
    c=$($here/disk.sh --print --cpu "$cpu")
    has "$c" "-drive file=build/disk.img,format=raw -monitor telnet:127.0.0.1:5555,server,nowait" && ok "disk: command line" || bad "disk: $c"

    # 2. serial: the log holds the test end line. The placeholder has no exit device here, so
    #    the timeout ends QEMU (124 is expected).
    rm -f "$BUILD/serial.log"
    $here/serial.sh --cpu "$cpu" --timeout 5 >/dev/null 2>&1; rc=$?
    if grep -Eq '^KTEST-END pass=[0-9]+ fail=0 ' "$BUILD/serial.log" 2>/dev/null; then
        ok "serial: log has KTEST-END (qemu rc=$rc)"; else bad "serial: no KTEST-END"; fi

    # 3. faults: the CPU reset at power-on is logged.
    rm -f "$BUILD/qemu.log"
    $here/faults.sh --cpu "$cpu" --headless --timeout 5 >/dev/null 2>&1
    if grep -q 'CPU Reset' "$BUILD/qemu.log" 2>/dev/null; then ok "faults: qemu.log has CPU Reset"; else bad "faults: qemu.log empty"; fi

    # 4. gdb: QEMU listens, and the guest does not run while no debugger is attached.
    port=$((12340 + RANDOM % 1000))
    $here/gdb.sh --cpu "$cpu" --headless --timeout 20 --port "$port" >"$BUILD/gdb-profile.out" 2>&1 &
    pid=$!
    if wait_port "$port" 5; then
        sleep 2
        if [ ! -s "$BUILD/gdb-profile.out" ]; then ok "gdb: port $port open, guest silent (halted)"; else bad "gdb: guest output while halted"; fi
    else bad "gdb: port $port never opened"; fi
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null

    # 5. disk: the drive and the monitor are there. 'info block' is answered by QEMU itself.
    rm -f "$BUILD/disk.img"
    mport=$((13340 + RANDOM % 1000))
    $here/disk.sh --cpu "$cpu" --headless --timeout 20 --mport "$mport" >/dev/null 2>&1 &
    pid=$!
    if wait_port "$mport" 5; then
        out=$( { exec 3<>"/dev/tcp/127.0.0.1/$mport"; sleep 0.5; echo 'info block' >&3; sleep 1; timeout 1 cat <&3 | tr -d '\0'; } 2>/dev/null)
        if has "$out" "build/disk.img" && has "$out" "raw"; then ok "disk: info block lists disk.img (raw)"; else bad "disk: info block gave: $out"; fi
    else bad "disk: monitor port $mport never opened"; fi
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
done

[ "$fail" -eq 0 ] && echo "qemu-selftest: PASS" || echo "qemu-selftest: FAIL"
exit $fail

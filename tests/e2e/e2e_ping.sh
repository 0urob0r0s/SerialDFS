#!/usr/bin/env bash
# Phase 2 end-to-end ping test (86Box edition).
#
# Topology:
#   SERPING.EXE (DOS, in 86Box)  --COM1--> 86Box openpty() slave
#   /dev/pts/N (slave path from 86Box log) <- start_86box_bridge.sh symlinks
#                                              /tmp/linux-com1 -> /dev/pts/N
#   /tmp/linux-com1               <-pyserial-> echo_server.py
#
# 86Box's serial1_passthrough_enabled creates a PTY pair on each VM
# launch and writes the slave path to its log. The bridge script polls
# the log for that path and symlinks it. echo_server uses pyserial on
# /tmp/linux-com1 unchanged from the DOSBox-X era.
#
# Timing: 86Box logs the slave path *very* early (before BIOS POST), so
# by the time AUTOEXEC runs SERPING ~30 s later, the bridge symlink and
# echo server are long since up.
set -uo pipefail

PASS=0; FAIL=0
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VM_PATH="${BOX86_VM_PATH:-/dos/c}"
SERPING_OUT=/tmp/serping.out

check() {
    local desc="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -qF "$expected"; then
        echo "  PASS: $desc"; PASS=$((PASS+1))
    else
        echo "  FAIL: $desc"
        echo "        expected: $expected"
        echo "        got:      $actual"
        FAIL=$((FAIL+1))
    fi
}

cleanup() {
    [ -n "${ECHO_PID:-}" ] && kill "$ECHO_PID" 2>/dev/null || true
    [ -n "${CMD_PID:-}" ] && kill "$CMD_PID" 2>/dev/null || true
    86box-run stop 2>/dev/null || true
    rm -f "$SERPING_OUT"
}
trap cleanup EXIT

echo "=== SerialDFS Phase 2: 86Box PTY-passthrough ping ==="

# 0. Install DOS build artifacts into dos.img (86Box has no host-mount).
86box-run stop 2>/dev/null || true
sleep 1
bash "$ROOT/tests/e2e/_install_dos.sh" >/dev/null

# 1. Sanity-check cfg
check "86box.cfg has serial1 passthrough enabled" "serial1_passthrough_enabled = 1" \
    "$(grep -E 'serial1_passthrough_enabled' "$VM_PATH/86box.cfg" 2>/dev/null || echo MISSING)"

# 2. Launch 86box-cmd in background. It cold-boots 86Box, which opens the
#    serial PTY and writes "Slave side is /dev/pts/N" to the log. AUTOEXEC
#    will then run SERPING and write its output to A:\OUT.TXT — captured
#    by 86box-cmd to stdout.
86box-cmd "C:\\SERDFS\\DOS\\BUILD\\SERPING.EXE COM1 9600" > "$SERPING_OUT" 2>&1 &
CMD_PID=$!

# 3. Bridge: wait for the slave path to appear in the log, then symlink
#    /tmp/linux-com1 -> /dev/pts/N. This is fast (86Box logs the slave
#    path long before DOS even boots).
bash "$ROOT/linux/ptybridge/start_86box_bridge.sh"
check "linux-com1 PTY slave linked" "/tmp/linux-com1" "$(ls -l /tmp/linux-com1 2>&1 || true)"

# 4. Start the echo server on the symlinked PTY. By the time SERPING
#    runs (~25–30 s into the AUTOEXEC sequence), the echo server has
#    been listening for many seconds. Stderr swallowed so the routine
#    "device disconnected" exception when 86Box exits doesn't pollute
#    the test output.
python3 "$ROOT/linux/echo/echo_server.py" /tmp/linux-com1 9600 2>/dev/null &
ECHO_PID=$!
sleep 0.3

# 5. Wait for 86box-cmd to finish (success = "OK" in OUT.TXT).
wait "$CMD_PID" 2>/dev/null || true
PING_OUT="$(cat "$SERPING_OUT" 2>/dev/null || echo MISSING)"
check "SERPING returns OK" "OK" "$PING_OUT"

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1

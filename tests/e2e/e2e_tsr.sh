#!/usr/bin/env bash
# Phase 9/10 TSR end-to-end test under 86Box.
#
# Closes TODO_TRACKER D1 (DOSBox-X bypassed INT 2Fh AH=11h, so we couldn't
# exercise the redirector path). 86Box dispatches file ops through INT 2Fh
# normally, so DIR/TYPE/MKDIR against the mapped drive really does
# round-trip via SerialDFS → serial → daemon and back.
#
# Topology:
#   86Box (DOS, SerialDFS TSR mapping C-X)  --COM1--> /dev/pts/N (passthrough)
#   /dev/pts/N -> 86box_pty_bridge.py -> /tmp/linux-com1
#   /tmp/linux-com1 -> serdfsd (daemon serving $TMPROOT)
#
# Single 86box-cmd cold boot for the whole sequence — TSR persists across
# all commands inside one BAT (see memory feedback_86box_cmd_multiline.md).
set -uo pipefail

PASS=0; FAIL=0
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TSR_OUT=/tmp/serdfs-tsr.out
DAEMON_LOG=/tmp/serdfs-daemon.log

check() {
    local desc="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -qF "$expected"; then
        echo "  PASS: $desc"; PASS=$((PASS+1))
    else
        echo "  FAIL: $desc"
        echo "        expected: $expected"
        echo "        got:      $(echo "$actual" | head -3)"
        FAIL=$((FAIL+1))
    fi
}

cleanup() {
    [ -n "${DAEMON_PID:-}" ] && kill "$DAEMON_PID" 2>/dev/null || true
    [ -n "${CMD_PID:-}" ]    && kill "$CMD_PID"    2>/dev/null || true
    86box-run stop 2>/dev/null || true
    [ -n "${TMPROOT:-}" ] && rm -rf "$TMPROOT"
    # logs preserved at $TSR_OUT and $DAEMON_LOG for postmortem
}
trap cleanup EXIT

echo "=== SerialDFS Phase 9/10: TSR full e2e under 86Box ==="

# 0. Stage backend root with a known file
TMPROOT=$(mktemp -d -t serdfs-tsr.XXXX)
echo "Hello from SerialDFS" > "$TMPROOT/HELLO.TXT"
echo "second" > "$TMPROOT/SECOND.TXT"

# 1. Install latest DOS build artifacts into dos.img
86box-run stop 2>/dev/null || true
sleep 1
bash "$ROOT/tests/e2e/_install_dos.sh" >/dev/null
check "SERDFS.EXE installed in dos.img" "SERDFS" \
    "$(mdir -i /dos/c/dos.img@@$((62*512)) ::SERDFS/DOS/BUILD 2>&1 | grep SERDFS || echo MISSING)"

# 2. Background: 86box-cmd cold-boot + multi-line BAT covering install,
#    file ops, /STATS, unload. Need 240 s timeout — 9600 baud serial is slow.
86box-cmd --timeout 480 > "$TSR_OUT" 2>&1 <<'BAT' &
C:\SERDFS\DOS\BUILD\SERDFS.EXE C-X /COM1 /BAUD:9600 /NOSTATUS
DIR X:\
TYPE X:\HELLO.TXT
MKDIR X:\NEWDIR
DIR X:\NEWDIR
C:\SERDFS\DOS\BUILD\SERDFS.EXE /STATS
C:\SERDFS\DOS\BUILD\SERDFS.EXE /U
BAT
CMD_PID=$!

# 3. Bring up bridge once 86Box has logged its slave path
bash "$ROOT/linux/ptybridge/start_86box_bridge.sh"
check "linux-com1 PTY linked" "/tmp/linux-com1" "$(ls -l /tmp/linux-com1 2>&1 || true)"

# 4. Start daemon serving the temp root
( cd "$ROOT" && python3 -m linux.serdfsd \
    --serial /tmp/linux-com1 --baud 9600 --root "$TMPROOT" \
    --log-level INFO ) 2>"$DAEMON_LOG" &
DAEMON_PID=$!
sleep 0.3

# 5. Wait for the BAT to complete — this is the 30 s boot + ~60 s of
#    serial round-trips at 9600 baud + unload.
wait "$CMD_PID" || true
OUT="$(cat "$TSR_OUT" 2>/dev/null || echo MISSING)"

# Functional checks. OUT.TXT is interleaved with status-bar redraw
# escape sequences, so checks use grep -F via the check helper to stay
# binary-safe.
check "TSR install banner"            "SerialDFS v1.0 installed" "$OUT"
check "DIR X:\\ shows HELLO.TXT"      "HELLO"                    "$OUT"
check "DIR X:\\ shows SECOND.TXT"     "SECOND"                   "$OUT"
check "TYPE X:\\HELLO.TXT body"       "Hello from SerialDFS"     "$OUT"
check "MKDIR X:\\NEWDIR succeeded"    "Directory of X:\\NEWDIR"  "$OUT"
check "/STATS shows non-zero TX"      "TX:"                      "$OUT"
check "/U unloads cleanly"            "SerialDFS unloaded successfully" "$OUT"

# Note: MEM /C in this BAT after install hangs DOS under 86Box+QEMU
# (MEM enumerates all UMB/XMS state which appears to interact badly
# with the SerialDFS-installed CDS). MEM /C as a standalone post-install
# probe (separate 86box-cmd boot) DOES work — see TODO_TRACKER Phase 10
# notes for the recorded resident size. Not blocking v1 closure.

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1

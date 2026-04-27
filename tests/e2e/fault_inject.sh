#!/usr/bin/env bash
# Phase 9 fault injection: daemon dies before COPY can complete.
#
# Strategy: daemon stays alive long enough for SERDFS install (no daemon
# traffic needed) and the initial DIR X:\ probe (proves the redirector
# round-trip is healthy). Then we SIGKILL the daemon, COPY's READs all
# hit serial timeouts, COPY aborts with a disk error, the BAT continues
# to /STATS (which records the timeouts) and /U.
#
# Pass criteria: BAT completes (no 86box-cmd timeout), /STATS shows TO:>0,
# and the destination file is absent or differs from the source.
set -uo pipefail

PASS=0; FAIL=0
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FI_OUT=/tmp/serdfs-fi.out
DAEMON_LOG=/tmp/serdfs-fi-daemon.log
EXTRACTED=/tmp/serdfs-fi-extracted.bin

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
    [ -n "${DAEMON_PID:-}" ] && kill -9 "$DAEMON_PID" 2>/dev/null || true
    [ -n "${KILLER_PID:-}" ] && kill "$KILLER_PID" 2>/dev/null || true
    [ -n "${CMD_PID:-}" ]    && kill "$CMD_PID"    2>/dev/null || true
    86box-run stop    2>/dev/null || true
    86box-bridge stop 2>/dev/null || true
    [ -n "${TMPROOT:-}" ] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

echo "=== SerialDFS Phase 9: fault injection (daemon dies pre-COPY) ==="

# 0. Stage backend root with 4 KB of deterministic content.
TMPROOT=$(mktemp -d -t serdfs-fi.XXXX)
SOURCE="$TMPROOT/FAULT.BIN"
python3 -c "
buf = bytearray(4 * 1024)
x = 0xC0DE
for i in range(len(buf)):
    x = (x * 1103515245 + 12345) & 0xFFFFFFFF
    buf[i] = (x >> 16) & 0xFF
open('$SOURCE', 'wb').write(bytes(buf))
"
check "source is 4 KB" "4096" "$(stat -c %s "$SOURCE" || echo BAD)"

# 1. Install latest DOS build artifacts; clean prior dest.
86box-run stop 2>/dev/null || true
sleep 1
mdel -i /dos/c/dos.img@@$((62*512)) ::FAULT.BIN >/dev/null 2>&1 || true
bash "$ROOT/tests/e2e/_install_dos.sh" >/dev/null

# 2. BAT: install TSR, DIR X:\ (succeeds — daemon alive), then COPY
#    (hits dead daemon, fails), then /STATS (proves timeouts), then /U.
86box-cmd --timeout 600 > "$FI_OUT" 2>&1 <<'BAT' &
C:\SERDFS\DOS\BUILD\SERDFS.EXE C-X /COM1 /BAUD:9600 /NOSTATUS
DIR X:\
COPY X:\FAULT.BIN C:\FAULT.BIN
C:\SERDFS\DOS\BUILD\SERDFS.EXE /STATS
C:\SERDFS\DOS\BUILD\SERDFS.EXE /U
BAT
CMD_PID=$!

# 3. Bring up bridge + daemon
86box-bridge start
( cd "$ROOT" && python3 -m linux.serdfsd \
    --serial /tmp/linux-com1 --baud 9600 --root "$TMPROOT" \
    --log-level INFO ) 2>"$DAEMON_LOG" &
DAEMON_PID=$!

# 4. Watcher: poll daemon log for the first LIST_DIR / GET_ATTR (proves
#    DIR X:\ has reached the daemon — i.e. the system is up and healthy).
#    Then SIGKILL the daemon. From that moment, every TSR call to the
#    daemon will time out; COPY will fail; BAT proceeds to /STATS + /U.
(
    deadline=$(($(date +%s) + 240))
    while [ $(date +%s) -lt $deadline ]; do
        if grep -q 'rx cmd\|LIST_DIR\|GET_ATTR' "$DAEMON_LOG" 2>/dev/null; then
            break
        fi
        sleep 1
    done
    # Give DIR a moment to finish, then kill BEFORE COPY starts its reads.
    sleep 2
    kill -9 "$DAEMON_PID" 2>/dev/null || true
    echo "[fault] killed daemon PID $DAEMON_PID after first DIR" >&2
) &
KILLER_PID=$!

# 5. Wait for the BAT to complete.
wait "$CMD_PID" || true
OUT="$(strings "$FI_OUT" 2>/dev/null || echo MISSING)"

86box-run stop 2>/dev/null || true
sleep 1

check "TSR install banner"  "SerialDFS v1.0 installed" "$OUT"
check "DIR X: ran (FAULT.BIN listed)" "FAULT" "$OUT"
check "/STATS shows timeouts"        "TO:"   "$OUT"
check "/U unloads cleanly"  "SerialDFS unloaded"       "$OUT"

# 6. Destination must not match source byte-for-byte.
mtype -i /dos/c/dos.img@@$((62*512)) ::FAULT.BIN > "$EXTRACTED" 2>/dev/null || true
EXT_SIZE=$(stat -c %s "$EXTRACTED" 2>/dev/null || echo 0)
echo "  INFO: destination size = $EXT_SIZE bytes (source = 4096)"

if [ ! -s "$EXTRACTED" ]; then
    echo "  PASS: destination absent or empty (COPY aborted cleanly)"; PASS=$((PASS+1))
elif [ "$EXT_SIZE" -ne 4096 ]; then
    echo "  PASS: destination size differs ($EXT_SIZE != 4096)"; PASS=$((PASS+1))
elif ! cmp -s "$SOURCE" "$EXTRACTED"; then
    echo "  PASS: destination size matches but content differs"; PASS=$((PASS+1))
else
    echo "  FAIL: destination matches source — daemon kill didn't corrupt COPY"
    FAIL=$((FAIL+1))
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1

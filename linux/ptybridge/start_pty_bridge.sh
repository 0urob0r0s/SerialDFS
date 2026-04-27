#!/usr/bin/env bash
# Idempotent nullmodem bridge launcher.
# Starts nullmodem_bridge.py which:
#   - Listens on TCP 127.0.0.1:5555 for DOSBox-X nullmodem connections
#   - Exposes /tmp/linux-com1 PTY slave for the echo server / daemon
# DOSBox-X must have: serial1 = nullmodem server:127.0.0.1 port:5555
# Safe to call multiple times; does nothing if bridge is already running.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BRIDGE="$SCRIPT_DIR/nullmodem_bridge.py"
PORT=5555
LINK=/tmp/linux-com1
LOG=/tmp/nullmodem-bridge.log

if pgrep -f "nullmodem_bridge.py" > /dev/null 2>&1; then
    echo "Nullmodem bridge already running."
else
    python3 "$BRIDGE" --port "$PORT" --link "$LINK" \
        >> "$LOG" 2>&1 &
    echo "Nullmodem bridge started (PID $!)."
fi

# Wait up to 5 s for the PTY slave symlink (R5 mitigation)
WAIT=0
until [ -e "$LINK" ]; do
    sleep 0.1
    WAIT=$((WAIT + 1))
    if [ "$WAIT" -ge 50 ]; then
        echo "ERROR: Nullmodem bridge did not create $LINK after 5 s" >&2
        cat "$LOG" >&2 2>/dev/null || true
        exit 1
    fi
done

echo "Bridge ready: $LINK -> $(readlink "$LINK")"

#!/usr/bin/env bash
# Phase 1 smoke tests — development environment validation under 86Box.
# Run from any directory: bash tests/e2e/smoke.sh
#
# History: originally a DOSBox-X smoke; ported to 86Box on 2026-04-26.
# The "D: mount" check is gone — 86Box has no host-mount feature; we
# install host files into dos.img at C:\SERDFS\... via 86box-install-dos
# instead.
set -uo pipefail

PASS=0
FAIL=0
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

check() {
    local desc="$1"; local expected="$2"; local actual="$3"
    if echo "$actual" | grep -qF "$expected"; then
        echo "  PASS: $desc"
        PASS=$((PASS+1))
    else
        echo "  FAIL: $desc"
        echo "        expected to find: $expected"
        echo "        got: $actual"
        FAIL=$((FAIL+1))
    fi
}

echo "=== SerialDFS Phase 1 smoke tests (86Box) ==="

# 1. Open Watcom is available
WCL_OUT=$(wcl -? 2>&1 | head -2 || true)
check "wcl available" "Open Watcom C/C++ x86 16-bit" "$WCL_OUT"

# 2. The 86Box toolkit is installed and on PATH
check "86box-cmd on PATH"         "/usr/local/bin/86box-cmd"         "$(command -v 86box-cmd)"
check "86box-install-dos on PATH" "/usr/local/bin/86box-install-dos" "$(command -v 86box-install-dos)"
check "86box-bridge on PATH"      "/usr/local/bin/86box-bridge"      "$(command -v 86box-bridge)"

# 3. HELLO.EXE built (build it if not)
if [ ! -f "$ROOT/dos/build/HELLO.EXE" ]; then
    ( cd "$ROOT/dos" && wmake hello ) >/dev/null 2>&1 || true
fi
check "HELLO.EXE built" "HELLO.EXE" \
    "$(ls -la "$ROOT/dos/build/HELLO.EXE" 2>&1 || echo MISSING)"

# 4. HELLO.EXE installs + runs inside 86Box
86box-run stop 2>/dev/null || true
sleep 1
86box-install-dos --to 'C:\SERDFS\DOS\BUILD' --quiet \
    "$ROOT/dos/build/HELLO.EXE" >/dev/null
HELLO_OUT=$(86box-cmd 'C:\SERDFS\DOS\BUILD\HELLO.EXE' 2>&1 || true)
check "HELLO.EXE prints HELLO in DOS" "HELLO" "$HELLO_OUT"

# 5. C:\SERDFS lists from inside DOS
C_OUT=$(86box-cmd 'DIR C:\SERDFS' 2>&1 || true)
check "C:\\SERDFS lists in DOS" "SERDFS" "$C_OUT"

86box-run stop 2>/dev/null || true

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1

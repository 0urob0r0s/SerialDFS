#!/usr/bin/env bash
# Phase 1 smoke tests — development environment validation.
# Run from any directory: bash tests/e2e/smoke.sh
set -uo pipefail

PASS=0
FAIL=0

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

echo "=== SerialDFS Phase 1 smoke tests ==="

# 1. Open Watcom is available
WCL_OUT=$(wcl -? 2>&1 | head -2 || true)
check "wcl available" "Open Watcom C/C++ x86 16-bit" "$WCL_OUT"

# 2. HELLO.EXE runs in DOSBox
HELLO_OUT=$(dosbox-cmd 'C:\SERDFS\DOS\BUILD\HELLO.EXE' 2>&1 || true)
check "HELLO.EXE prints HELLO" "HELLO" "$HELLO_OUT"

# 3. C: shows SERDFS directory
C_OUT=$(dosbox-cmd 'dir c:' 2>&1 || true)
check "C: contains SERDFS" "SERDFS" "$C_OUT"

# 4. D: mount works
D_OUT=$(dosbox-cmd 'dir d:' 2>&1 || true)
check "D: mount accessible" "Directory of D:" "$D_OUT"

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1

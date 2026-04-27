#!/usr/bin/env bash
# Thin wrapper kept for backwards compatibility with existing tests.
# The actual install logic moved into the dev-sandbox toolkit as
# `86box-install-dos` (source: /workspace/tools/86box/install-dos.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
exec 86box-install-dos --to 'C:\SERDFS\DOS\BUILD' \
    --src "$ROOT/dos/build" --pattern '*.EXE'

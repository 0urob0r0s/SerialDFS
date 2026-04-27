#!/usr/bin/env bash
# Thin wrapper kept for backwards compatibility with existing tests.
# The actual bridge logic moved into the dev-sandbox toolkit as
# `86box-bridge` (source: /workspace/tools/86box/pty-bridge.py).
exec 86box-bridge start "$@"

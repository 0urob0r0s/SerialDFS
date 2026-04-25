# ADR-0002: Linux daemon language — Python

**Status:** Accepted  
**Date:** 2026-04-25

## Decision

`serdfsd` is written in Python 3 using pyserial.

## Rationale

- pyserial 3.5 is installed in the dev container; gcc/make are not.
- Python easily meets the 64 MB steady-state memory budget on Pi.
- The protocol codec (`frame.py`) is shared with test tooling and the Phase 2
  echo server, removing a parity-bug class.
- At 57600 baud, the Linux side has ~170 µs per byte; Python handles this without
  measurable latency.

## Fallback

If Pi-side performance proves inadequate on real hardware (Phase 11), port
`serdfsd` to C using `vendor/etherdfs-baseline/ethersrv-linux/` patterns.
The golden protocol vectors (Phase 3) lock the wire contract, making a mechanical
C port safe. This is a v1.x deliverable, not a v1 blocker.

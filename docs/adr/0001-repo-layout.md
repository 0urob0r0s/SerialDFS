# ADR-0001: Repository layout

**Status:** Accepted  
**Date:** 2026-04-25

## Decision

SerialDFS lives at `/dos/c/serdfs/` as a fully independent project, separate
from the EtherDFS source tree at `/dos/c/etherdfs/`.

## Rationale

- Clean namespace: no ambiguity between `etherdfs/trunk/` and new `dos/` source.
- Independence: build scripts, tests, and docs contain no runtime references to
  `/dos/c/etherdfs/`. The vendor snapshot inside `vendor/etherdfs-baseline/`
  is the only connection.
- The DOS-side build artifacts are installed into `dos.img` (the
  86Box C: drive) at `C:\SERDFS\DOS\BUILD\` via `86box-install-dos`.
  All DOS-bound paths remain 8.3-clean (max two directory levels
  deep under `C:\SERDFS\` before the `.EXE` name).

## Migration note (2026-04-26)

This ADR originally described `/dos/c` being reached via DOSBox-X's
host-mount feature. The dev-environment migration from DOSBox-X to
86Box dropped host mounts (86Box has no equivalent), but the
constraint and the `C:\SERDFS\…` path it produces are unchanged:
artifacts now arrive in dos.img through `86box-install-dos` (mtools
under the hood) and DOS sees them at the same paths. The ADR's
guidance still holds.

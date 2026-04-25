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
- DOSBox mounts `/dos/c` as `C:`, so the project is reachable at `C:\SERDFS\`.
  All DOS-bound paths remain 8.3-clean (max two directory levels deep under
  `C:\SERDFS\` before the `.EXE` name).

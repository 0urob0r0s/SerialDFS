# ADR-0005: Licence (GPL-2.0) and EtherDFS baseline strategy

**Status:** Accepted  
**Date:** 2026-04-25

## Decision

SerialDFS is licensed GPL-2.0. EtherDFS source files are vendored under
`vendor/etherdfs-baseline/` as a frozen, unedited snapshot for attribution
and traceability.

## Rationale

EtherDFS is GPL-2.0. SerialDFS adapts its DOS redirector skeleton
(`inthandler`, `process2f`, `dosstruc.h`, `chint086.asm`, etc.), which makes
SerialDFS a GPL derivative work. GPL-2.0 is the correct and legally required
licence for the combined work.

The vendor snapshot:
- Provides a clear git-traceable record of what was copied vs. authored.
- Makes the original-vs-adapted diff machine-checkable at any point.
- Satisfies the GPL requirement to make the corresponding source available.

Files in `vendor/` are never edited. Working copies in `dos/src/` carry
header comments citing the vendored file and line range they were derived from.
See `NOTICES` for the full attribution table.

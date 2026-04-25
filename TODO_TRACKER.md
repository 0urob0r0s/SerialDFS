# SerialDFS TODO Tracker

## Phase Status

| Phase | Status | Notes |
|---|---|---|
| 0 — Scaffolding | ✅ Done | Repo at /dos/c/serdfs/ (8.3-safe); HELLO.EXE builds and runs in DOSBox |
| 1 — Development Environment Validation | ✅ Done | wcl, DOSBox HELLO.EXE, C:/D: mounts verified; smoke.sh 4/4; runbook documents R4 failure modes |
| 2 — Serial PTY Connectivity | ⬜ Not started | — |
| 3 — Protocol Codec | ⬜ Not started | — |
| 4 — Linux Daemon Minimal Filesystem RPC | ⬜ Not started | — |
| 5 — DOS Client Non-Resident RPC Tooling | ⬜ Not started | — |
| 6 — Write Operations and Stable Name Map | ⬜ Not started | — |
| 7 — EtherDFS Redirector Study and Extraction | ⬜ Not started | — |
| 8 — Minimal TSR / Drive Mapping | ⬜ Not started | — |
| 9 — Full v1 Filesystem Operations | ⬜ Not started | — |
| 10 — Optional Status Bar and Metrics | ⬜ Not started | — |
| 11 — Real Hardware Validation | ⬜ Not started | — |

## Decisions Needed from User

| ID | Question | Context | Phase | Status |
|---|---|---|---|---|

## Discoveries & Deferred Items

| ID | Discovery | Impact | Deferred to |
|---|---|---|---|

## Assumptions Made

| ID | Assumption | Rationale | Reversible? |
|---|---|---|---|
| A1 | Linux daemon in Python (pyserial 3.5) | gcc/make not in container; pyserial ready; 64 MB budget met | Yes — C port documented as Phase 11 fallback in ADR-0002 |
| A2 | Direct 8250/16550 UART from Phase 2 | DOSBox-X directserial emulates chip for correctness testing; one transport impl across phases | No — shapes seruart.c from Phase 2 onward |
| A3 | CRC-16/CCITT-FALSE (0x1021, init 0xFFFF) | Well-known; trivially verifiable; 256-entry table fits 512 bytes | No — locked once vectors committed in Phase 3 |
| A4 | Repo at /dos/c/serdfs/, GPL-2.0, vendored EtherDFS baseline | Independent fork; EtherDFS is GPL-2.0 derivative; vendor/ provides attribution trail | No — git history and NOTICES are permanent |

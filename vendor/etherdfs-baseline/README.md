# EtherDFS Baseline Vendor Snapshot

This directory contains a frozen, unmodified snapshot of EtherDFS source files
used as the conceptual baseline for SerialDFS.

**Source:** `/dos/c/etherdfs/etherdfs/trunk/` (EtherDFS by Mateusz Viste)
**Date copied:** 2026-04-25
**License:** GPL-2.0 (see /dos/c/serdfs/COPYING)

## Purpose

SerialDFS adapts the EtherDFS DOS redirector shape (INT 2Fh dispatch, SDA/CDS/SFT
structures, TSR install/unload lifecycle) while replacing the Ethernet/packet-driver
transport with a new serial RPC protocol. These files are the provenance record for
that adaptation.

## Files

| File | Role in EtherDFS | SerialDFS use |
|------|-----------------|---------------|
| `etherdfs/trunk/etherdfs.c` | Main TSR: inthandler, process2f, pktdrv, init | REUSE inthandler/process2f skeleton; REPLACE sendquery+pktdrv |
| `etherdfs/trunk/globals.h` | Global vars, packet buffers, drive map | ADAPT: keep ldrv[], replace pkt buffers |
| `etherdfs/trunk/dosstruc.h` | DOS SDA/SDB/CDS/SFT layout structs | COPY VERBATIM |
| `etherdfs/trunk/chint.h` | _mvchain_intr declaration | COPY VERBATIM |
| `etherdfs/trunk/chint086.asm` | Interrupt chain helper stub | COPY VERBATIM |
| `etherdfs/trunk/genmsg.c` | String compaction tool | ADAPT |
| `etherdfs/trunk/makefile` | OpenWatcom build rules | ADAPT |
| `etherdfs/trunk/protocol.txt` | EtherDFS Ethernet wire format | REFERENCE ONLY — do not port |

## Do Not Edit

Files in this directory are frozen for attribution and reference. All working
copies live under `dos/src/`. See `docs/etherdfs-notes.md` for the full
REUSE / REPLACE / IGNORE analysis.

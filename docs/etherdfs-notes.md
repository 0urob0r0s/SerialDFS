# EtherDFS Redirector Study — REUSE / REPLACE / IGNORE

`vendor/etherdfs-baseline/etherdfs/trunk/etherdfs.c` (2011 lines, MIT)
Catalogued for SerialDFS Phase 7.  All line references are to that file.

This document is the R10 gate: no `pktdrv_*`, MAC, or Ethernet
reference may appear in Phase 8+ SerialDFS code.

---

## REUSE (verbatim or near-verbatim, with attribution header)

| Lines | Symbol / Region | Destination | Notes |
|-------|-----------------|-------------|-------|
| whole file | `dosstruc.h` | `dos/src/dosstruc.h` | Copied verbatim; DOS internal struct layouts (CDS/SDB/SFT/SDA). |
| whole file | `chint.h` | `dos/src/chint.h` | Copied verbatim; `_mvchain_intr()` declaration. |
| whole file | `chint086.asm` | `dos/src/chint086.asm` | Copied verbatim; 16-bit chain-intr helper from OpenWatcom 1.9. |
| 50–83 | `copybytes()`, `mystrlen()`, `len_if_no_wildcards()` | `dos/src/serdfs.c` | Utility helpers; port as-is, no packet references. |
| 166–175 | `AL_SUBFUNCTIONS` enum | `dos/src/serdfs.c` | INT 2Fh subfunction IDs; no changes needed. |
| 176–246 | `supportedfunctions[]` table | `dos/src/serdfs.c` | Maps subfunction to capability flags; keep as-is. |
| 399–400 | `SUCCESSFLAG` / `FAILFLAG` macros | `dos/src/serdfs.c` | AX result-setting macros; reuse verbatim. |
| 403–899 | `process2f()` | `dos/src/serdfs.c` | All ~22 case arms (CHDIR, CLSFIL, READFIL, DISKSPACE, GETATTR, OPEN, FINDFIRST/NEXT, WRITEFIL, MKDIR, RMDIR, DELETE, RENAME, CREATE, SPOPNFIL, CMMTFIL, SKFMEND, LOCKFIL); replace every `sendquery()` call site with `serial_rpc()`. |
| 902–1090 | `inthandler()` | `dos/src/serdfs.c` | INT 2Fh entry: DS-patch, register save, stack switch, drive validation, dispatch to `process2f()`. Replace the `pktdrv_recv` segment ID bytes at lines ~1013–1015 with `com_isr` segment ID; all other logic unchanged. |
| 1222–1243 | `getsda()` | `dos/src/serdfs.c` | Returns far pointer to DOS SDA; verbatim. |
| 1245–1285 | `getcds()` | `dos/src/serdfs.c` | Walks CDS array for the requested drive; verbatim. |
| 1290–1305 | `outmsg()`, `zerobytes()` | `dos/src/serdfs.c` | String output helper and zero-fill; verbatim. |
| 1448–1464 | `byte2hex()` | `dos/src/serdfs.c` | Hex-format byte; keep for any debug output. |
| 1466–1505 | `allocseg()`, `freeseg()` | `dos/src/serdfs.c` | DOS segment allocator/freer; verbatim. |
| 1509–1547 | `updatetsrds()` | `dos/src/serdfs.c` | Patches resident DS pointer in the ISR prologue. **Adaptation**: scan for `com_isr` signature bytes (the MOV AX,DS instruction opcode sequence) instead of the `pktr` packet-driver call target. |
| 1555–1589 | `findfreemultiplex()` | `dos/src/serdfs.c` | Scans INT 2Fh IDs 0xC0–0xFF for a free slot; verbatim. |
| 1591–2011 | `main()` — transient install skeleton | `dos/src/serdfs.c` | Keep: CDS drive-letter assignment loop, `getdosvermajor()`/`getdosverminor()` checks, `pspseg` capture, INT 2Fh chain save+hook, `allocseg`/`freeseg` TSR keep-resident sequence. Remove: all `pktdrv_init`, `pktdrv_accesstype`, `pktdrv_getaddr`, `string2mac` calls. Add: `seruart_init(port, div)` + IRQ hook before `_dos_keep()`. |
| whole file | `genmsg.c` structure | `dos/src/genmsg.c` | Adapted: SerialDFS product strings, drop packet-driver messages. |
| 23–28 | globals `dbg_VGA` pattern | `dos/src/serdfs.c` | Phase 10 status bar: direct `0xB8000:0` write; borrow the far-pointer cast. |

---

## REPLACE (semantic substitution — new implementation, same contract)

| Lines | EtherDFS symbol | SerialDFS replacement | Rationale |
|-------|-----------------|----------------------|-----------|
| 279–395 | `sendquery()` | `serial_rpc()` in `dos/src/serrpc.c` | Ethernet framing → serial framing; same retry/timeout shape but 3 retries / 36-tick window; replaces packet driver send+recv with `seruart_send_block` + `seruart_recv_byte_with_timeout`. |
| 84–101 | `bsdsum()` CRC | `crc16_update()` / `frame_encode/decode()` in `dos/src/serframe.c` | CRC-16/CCITT-FALSE replaces EtherDFS BSD sum; 256-entry table. |
| 60-byte Ethernet header layout | Ethernet frame | 10-byte SerialDFS header per `protocol/spec.txt §5.1` | Transport change. |
| `etherdfs.c:39–47` | `FRAMESIZE`, send/recv buffer sizes | `FRAME_MAX_SIZE`, `FRAME_MAX_PAYLOAD` in `dos/src/serframe.h` | Smaller serial frame (512-byte payload cap). |
| `globals.h` `pkthandle`/`pktint` fields | Packet driver handle/int | `comport` (I/O base), `baudcode` (divisor) in `tsrshareddata` | Serial port identity replaces packet driver handle. |
| `globals.h` `glob_pktdrv_recvbuff` / `sndbuff` / `pktcall` | Packet recv/send buffers | IRQ-driven RX ring in `dos/src/seruart.c` (`seruart_rxring[]`) | Ring is already resident; separate send buffer in `serrpc.c`. |
| 1350–1444 | `parseargv()` flag set | Adapted `parseargv()` in `dos/src/serdfs.c` | Flags: `/COM1`, `/COM2`, `/BAUD:N`, `/STATUS`, `/NOSTATUS`, `/UNLOAD`, `/QUIET`; remove `/p=XX`, `/n`, MAC argument. |

---

## IGNORE / DELETE (no equivalent needed in SerialDFS)

| Lines | Symbol | Reason |
|-------|--------|--------|
| 112–161 | `pktdrv_recv()` naked ISR | Ethernet packet-receive ISR; SerialDFS uses `com_isr` in `seruart.c`. The **register-save discipline** (naked, pushf/pusha, EOI pattern) is the only thing worth studying — the function itself is not ported. |
| 1104–1127 | `pktdrv_accesstype()` | Packet driver type-registration; no serial equivalent. |
| 1129–1165 | `pktdrv_getaddr()` | Retrieves local MAC; no serial equivalent. |
| 1167–1196 | `pktdrv_init()` | Opens packet driver; replaced by `seruart_init()`. |
| 1197–1220 | `pktdrv_free()` | Releases packet driver; replaced by UART cleanup / IRQ unhook. |
| `globals.h` `GLOB_LMAC` / `GLOB_RMAC` | MAC address macros | Ethernet-only. |
| `globals.h` `glob_pktdrv_pktcall` | Packet driver call vector | Not applicable. |
| 1309–1347 | `hexpair2int()`, `string2mac()` | MAC address parsing; not needed. |
| `etherdfs.c` `--nocksum` flag logic | Checksum-disable flag | SerialDFS always CRCs; ADR-0003 locks this. |
| `protocol.txt` Ethernet framing section | Ethernet wire format | Entire Ethernet framing description; SerialDFS protocol is documented in `protocol/spec.txt`. |

---

## R10 enforcement rule

**No file under `dos/src/` or `dos/tools/` may contain any of:**
- the substring `pktdrv` or `pktdrv_`
- the substring `GLOB_LMAC` or `GLOB_RMAC`
- the substring `string2mac` or `hexpair2int`
- an `#include` of a file not under `dos/src/` or `dos/tools/`

This list is checked by `grep` in the smoke.sh before Phase 8 can be declared done.

# SerialDFS User Manual

This is the per-executable reference for everything that ships in
`dos/build/` (DOS-side binaries) and `linux/` (host-side daemon +
bridge). Read [`README.md`](../README.md) first for the high-level
picture and [`docs/runbook.md`](runbook.md) for how to actually run
things end-to-end inside the 86Box sandbox.

All DOS executables are Open Watcom 16-bit DOS .EXEs (small memory
model, 8086-compatible). They run in 86Box (sandbox) or on real
DOS 3.3+ hardware.

---

## TSR (resident driver)

### `SERDFS.EXE` — the SerialDFS network redirector

Installs as an INT 2Fh AH=11h network redirector that forwards DOS file
operations on a mapped drive over RS-232 to a Linux daemon (`serdfsd`).

**Usage:**

```
SERDFS rdrv-ldrv [rdrv2-ldrv2 ...] /COMn /BAUD:rate [options]
SERDFS /STATS
SERDFS /STATUS
SERDFS /U
```

**Drive mapping** (one or more): `R-L` where `R` is the remote drive
letter the daemon serves under and `L` is the local DOS letter the
mapping appears as. Example: `C-X` maps the daemon's root to local
`X:`. The local letter must satisfy `LASTDRIVE` in `CONFIG.SYS` (the
sandbox patches `LASTDRIVE=Z` automatically).

**Options:**

| Flag | Meaning |
|---|---|
| `/COM1` \| `/COM2` | UART port to use. Default `/COM1`. |
| `/BAUD:N` | Baud rate. One of `9600`, `19200`, `38400`, `57600`, `115200`. Default `9600`. |
| `/STATUS` | Hook INT 28h + INT 1Ch and render the per-second `SDF TX:n RX:n TO:n RT:n CE:n` status line at the bottom of the screen. |
| `/NOSTATUS` | Disable the status line (default). |
| `/Q` | Quiet — suppress install/unload banner. |
| `/U` | Unload the resident TSR (no drive map needed; INT 2Fh chain restored). |
| `/STATS` | Query the running TSR via INT 2Fh AX=11FFh (multiplex AL=1) and print one-shot counters: `TX:bytes RX:bytes TO:timeouts RT:retries CE:crc-errors`. No file ops; just dumps the current /STATS shareddata. |

**Examples:**

```
SERDFS C-X /COM1 /BAUD:9600 /NOSTATUS         install
SERDFS A-Y B-Z /COM2 /BAUD:38400 /STATUS      two drives, status line on
SERDFS /STATS                                 print live counters
SERDFS /U                                     unload
```

**Resident size:** ~11 KB on DOS 6.22 (`MEM /C` confirms the install
fits well under the spec-§15 64 KB cap).

**Pre-requisites:**
- `LASTDRIVE` in `CONFIG.SYS` must reach the requested local drive letter.
- A Linux daemon (`serdfsd`) must be reachable at the COM port's other end.
- The sandbox uses `86box-bridge --port 1` to publish the COM1 PTY at
  `/tmp/linux-com1` for the daemon to open.

**Exit codes:**
- `0` — installed (or unloaded) successfully.
- `1` — argument error (usage printed).
- `2` — install precondition failure (LASTDRIVE too low, drive already
  owned, install-check found another SerialDFS already resident, or the
  serial port doesn't respond at all).

---

## Non-resident filesystem tools (Phase 5–6)

These talk SerialDFS protocol directly via `serrpc` (no TSR install
required). Useful for testing the daemon side without paying the TSR
install cost, and for scripting.

### `SERPING.EXE` — connectivity smoke

```
SERPING COM1|COM2 9600|19200|38400|57600
```

Sends a `CMD_PING` frame, expects a PING response with `STATUS_OK`.
Prints `OK (N ticks)` on success or `FAIL <reason>` on failure (CRC
mismatch, timeout, magic mismatch, etc.).

Used by `tests/e2e/e2e_ping.sh` (3/3 PASS in 86Box).

### `SERDIR.EXE` — list a remote directory

```
SERDIR COM1|COM2 9600|... [\PATH]
```

Sends `CMD_LIST_DIR` then loops `CMD_LIST_DIR_NEXT` until EOF, printing
one entry per line as `NAME size attr`. `\PATH` defaults to `\` (the
daemon's root). Phase 5 exerciser; useful when debugging the daemon's
namemap.

### `SERTYPE.EXE` — read a remote file

```
SERTYPE COM1|COM2 9600|... \PATH\FILE
```

Opens the file via `CMD_OPEN` (mode 0 = read-only), reads in 512-byte
chunks via `CMD_READ`, writes content to stdout, closes via `CMD_CLOSE`.
Phase 5 multi-chunk exerciser; verified against a 5767-byte BIGFILE.TXT.

### `SERCOPY.EXE` — write a local file to the daemon

```
SERCOPY COM1|COM2 9600|... LOCAL_SRC \REMOTE_DST
```

Reads `LOCAL_SRC` from the local DOS filesystem and writes it to the
daemon at `\REMOTE_DST` via `CMD_CREATE` (mode=1 = truncate-or-create) +
chunked `CMD_WRITE` (511-byte payloads, the largest the protocol allows
once you subtract the handle byte) + `CMD_CLOSE` (with timestamp
propagation). Phase 6 exerciser.

### `SERDEL.EXE` — delete a remote file

```
SERDEL COM1|COM2 9600|... \REMOTE_FILE
```

Sends `CMD_DELETE`. Phase 6 exerciser.

### `SERREN.EXE` — rename / move a remote file or directory

```
SERREN COM1|COM2 9600|... \OLD_PATH \NEW_PATH
```

Sends `CMD_RENAME` with `old\0new\0` payload (no drive byte). Phase 6
exerciser.

---

## Diagnostic + protocol-conformance tools

### `CRCFUZZ.EXE` — frame codec golden-vector tester

```
CRCFUZZ
```

Loads the five golden vectors from `C:\SERDFS\PROTOCOL\VECTORS\`
(`PNGREQ.BIN`, `PNGRESP.BIN`, `BADCRC.BIN`, `TRUNC.BIN`, `STALE.BIN`)
and asserts `frame_decode` outcomes against the expected status code.

Pass: `Results: 12 passed, 0 failed`, exits 0.
Fail: prints failing checks, exits 1.

Validates the CRC-16/CCITT-FALSE codec implementation against the
locked spec (ADR-0003). Mirror of the host-side `pytest test_frame.py`.

### `INT2FT.EXE` — direct INT 2Fh AH=11h dispatcher

```
INT2FT
```

Issues `INT 2Fh AX=111Bh` (FINDFIRST equivalent) directly with hand-set
registers and reports what AX comes back as. Used during the Phase 8
TSR install-check debugging to verify the multiplex handler was being
hit. Trivial program — no arguments, no environment.

### `I2FEXER.EXE` — INT 2Fh exerciser with SDA setup

```
I2FEXER GETATTR <drive>:\<path>
```

Sets up the SDA fields the way DOS would (fn1 path, fcb_fn1 expanded
name, srch_attr) then invokes INT 2Fh AH=11h directly. Was written
during the DOSBox-X era because DOSBox-X bypassed INT 2Fh dispatch
entirely (TODO_TRACKER D1) — it let us verify the TSR's redirector
path even though normal DOS commands didn't. Largely obsolete in
86Box-land (the regular `DIR X:\` etc. exercise the same path), but
kept for cases where you need precise control over SDA contents.

### `LSTEST.EXE` — sanity binary

Prints a short test string. Used early in Phase 1 to verify Open
Watcom + dos.img + 86box-cmd produced runnable EXEs at all.

### `HELLO.EXE` — Phase 0 "build chain works" probe

Prints `HELLO`. Cribbed by the dev-environment template's
`/workspace/examples/hello/`.

---

## Linux-side daemon

### `serdfsd` (`python3 -m linux.serdfsd`)

The Linux side of SerialDFS. Listens on a serial device (a PTY — the
sandbox uses `/tmp/linux-com1` published by `86box-bridge`), services
SerialDFS RPC frames, and serves files from a backend root.

```
python3 -m linux.serdfsd \
    --serial /tmp/linux-com1 \
    --baud 9600 \
    --root /path/to/served/dir \
    [--name-mode compat|strict-prefix] \
    [--log-level DEBUG|INFO|WARNING|ERROR] \
    [--rebuild-namemap]
```

**Flags:**

| Flag | Default | Meaning |
|---|---|---|
| `--serial` | `/tmp/linux-com1` | Serial device. Inside the sandbox this is a PTY published by `86box-bridge`; on real hardware it's an FTDI / UART device. |
| `--baud` | `9600` | Must match the SERDFS install side. |
| `--root` | (required) | Backend directory. Files under this dir are exposed; namemap (8.3 ↔ long-name aliases) lives here as `.SERDFS_NAMEMAP`. |
| `--name-mode` | `compat` | 8.3 alias generation. `compat` = EtherDFS-style; `strict-prefix` = stricter shortname rules. |
| `--log-level` | `INFO` | DEBUG dumps every `rx cmd=...` request. |
| `--rebuild-namemap` | off | Treat any existing namemap as corrupt and rebuild from scratch. |

**Run from the SerialDFS project root** so the `linux.serdfsd` package
import resolves:

```
cd /dos/c/serdfs && python3 -m linux.serdfsd --serial /tmp/linux-com1 \
    --baud 9600 --root /tmp/myroot --log-level INFO
```

Exits cleanly on SIGTERM / SIGINT (logs `shutting down (signal N)`).

---

## Linux-side helpers

### `linux/echo/echo_server.py` — protocol echo

```
python3 linux/echo/echo_server.py /tmp/linux-com1 9600 [--raw]
```

Speaks the SerialDFS frame protocol: reads `CMD_PING` frames, replies
with `CMD_PING` + `STATUS_OK`. Used by `tests/e2e/e2e_ping.sh`. With
`--raw`, falls back to byte-echo (legacy Phase 2 mode, kept for
diagnostic use).

### `linux/echo/test_client.py` — counterpart sender

```
python3 linux/echo/test_client.py /tmp/linux-com1 9600
```

Sends a PING frame and reports the response. Useful for testing the
echo server without involving DOS.

### `linux/ptybridge/start_86box_bridge.sh` — bridge launcher (legacy wrapper)

Thin wrapper that calls `86box-bridge start` (the actual bridge is
in the dev-environment toolkit at `/workspace/tools/86box/pty-bridge.py`).
Kept for backwards-compat with existing tests; new code should call
`86box-bridge start` directly.

(The DOSBox-X-era `nullmodem_bridge.py` and `start_pty_bridge.sh`
were deleted in the 2026-04-27 doc cleanup; the 86Box `pty-bridge.py`
in the dev-environment toolkit is the current implementation.)

---

## End-to-end test scripts (`tests/e2e/`)

| Script | What it asserts |
|---|---|
| `e2e_ping.sh` | SERPING round-trip via SerialDFS frame protocol. **3/3 PASS.** |
| `e2e_tsr.sh` | install + DIR + TYPE + MKDIR + 2nd DIR + /STATS + /U through the redirector. **9/9 PASS.** |
| `e2e_copy.sh` | 8 KB binary round-trip with byte-for-byte cmp match. **6/6 PASS.** (Spec's 64 KB target deferred to Phase 11 / real hardware — see TODO_TRACKER.) |
| `fault_inject.sh` | Daemon killed mid-test → TSR sees timeouts → COPY aborts cleanly → /STATS shows `TO:N`. **6/6 PASS.** |
| `_install_dos.sh` | Helper. mcopies `dos/build/*.EXE` into `dos.img:C:\SERDFS\DOS\BUILD\` (wraps `86box-install-dos`). |
| `smoke.sh` | Phase 1 dev-environment validation. Run from a fresh container to verify wcl + dos.img + 86box-cmd are all happy. |

Run a single test with `bash tests/e2e/<script>.sh`. Total runtime is
~3-5 minutes per script (one ~30 s 86Box cold boot per call, plus the
9600-baud serial round-trip times).

---

## Build

From the repo root:

```
make dos        # builds all DOS .EXE targets into dos/build/
make hello      # just dos/build/HELLO.EXE
make daemon ARGS='--serial /tmp/linux-com1 --baud 9600 --root /tmp/r'
make vectors    # regenerate protocol/vectors/ from the Python reference codec
make e2e        # run all tests/e2e/*.sh
make clean
```

The DOS-side build uses Open Watcom (`wcl -bt=dos -ms -0 -os`) under
the hood; see `dos/makefile` for the targets. The Linux-side daemon is
pure Python (pyserial 3.5; ships in the dev-environment Docker image).

---

## Where each piece runs

```
HOST (Linux container)                     86Box (DOS guest)
───────────────────────                    ─────────────────
  serdfsd ────┐                              ┌──── SERDFS.EXE (TSR)
              │                              │            │
              │                              │            └─ INT 2Fh AH=11h
   /tmp/linux-com1 (PTY)                     │               redirector
              │                              │
   86box-bridge ←──── /dev/pts/N ──── 86Box COM1 (8250/16550 emulator)
              │
              └── raw termios + 4 ms/byte throttle
```

The COM1 PTY is published by `86box-bridge --port 1` (the dev sandbox
defaults). For the persistent-DOS REPL (`86box-pcmd`), COM2 is wired
up the same way at `/tmp/linux-com2` via `86box-bridge --port 2`. They
coexist on the same VM so you can debug SerialDFS interactively while
keeping the redirector channel live.

# SerialDFS Runbook

> **Migration note (2026-04-26):** the original runbook described a
> DOSBox-X-based development loop with `dosbox-cmd`, `start_pty_bridge.sh`,
> and `/tmp/dosbox-com1`. The sandbox migrated to **86Box** (real BIOS,
> real INT 2Fh dispatch — see TODO_TRACKER D1). This file has been
> rewritten for the current toolchain. Tools mentioned here are
> documented in detail in [`USERMANUAL.md`](USERMANUAL.md) and (for the
> dev-environment side) in `/workspace/tools/86box/README.md`.

## Building DOS binaries

```bash
cd /dos/c/serdfs/dos
wmake hello        # build HELLO.EXE smoke pin
wmake all          # build all DOS .EXE targets into dos/build/
```

The container has `wmake` (Open Watcom), not GNU `make`. Use `wmake`
for all DOS-side builds. (Top-level `make dos` from the repo root just
chains into `wmake all`.)

## Running DOS commands in 86Box

There are three execution modes; pick by need:

### One-shot (cold-boot per call, ~30 s/call)

```bash
86box-cmd 'C:\SERDFS\DOS\BUILD\HELLO.EXE'
86box-cmd 'DIR C:\\'
```

`86box-cmd` boots 86Box from cold, runs the BAT, captures `A:\OUT.TXT`
to its stdout, kills 86Box. State does not persist between calls.

### Multi-step session (still one cold boot, but several DOS commands)

For tests that need TSR state to survive across commands (install +
ops + unload), pipe a multi-line BAT into a single `86box-cmd` call:

```bash
86box-cmd <<'BAT'
C:\SERDFS\DOS\BUILD\SERDFS.EXE C-X /COM1 /BAUD:9600 /NOSTATUS
DIR X:\
TYPE X:\HELLO.TXT
C:\SERDFS\DOS\BUILD\SERDFS.EXE /STATS
C:\SERDFS\DOS\BUILD\SERDFS.EXE /U
BAT
```

This is the model `tests/e2e/e2e_tsr.sh` uses (9/9 PASS).

### Persistent session (sub-second per call after one ~30 s boot)

For interactive iteration:

```bash
86box-pcmd start                    # boot + bring up COM2 REPL
86box-pcmd run 'VER'                # ~0.7 s
86box-pcmd run 'DIR C:\\'           # ~3 s
86box-pcmd stop
```

TSR state persists across `pcmd run` calls (TSRs hook the global IVT).
Environment vars + CWD do not (each `run` is a fresh `COMMAND.COM /C`
child). For SerialDFS-specific TSR testing, the `86box-cmd` multi-line
pattern above is more reliable — see TODO_TRACKER D8 for the gory
details.

### Drives inside DOS

The 86Box VM has a single physical disk (`C:`) seeded from the bundled
MS-DOS 6.22 template. There is no `D:` host-mount — that was a DOSBox-X
feature. To get host files into the DOS image, use:

```bash
86box-run stop                                 # required: no concurrent writes
86box-install-dos --to 'C:\SERDFS\DOS\BUILD' \
                  --src dos/build --pattern '*.EXE'
```

The `tests/e2e/_install_dos.sh` helper wraps this for the SerialDFS
test layout. All DOS paths must be 8.3-safe; the project root is
`C:\SERDFS\` (6 chars).

## 86box-cmd output capture — failure modes

`86box-cmd` builds a `RUN.BAT` on a virtual floppy (`AGENT.IMG`),
boots 86Box, lets AUTOEXEC `CALL` it, captures `A:\OUT.TXT` and the
`A:\DONE` completion marker, then prints OUT.TXT to its stdout.

| Symptom | Likely cause | Where to look |
|---|---|---|
| `Bad command or filename - "X"` | Path doesn't exist in dos.img, or isn't 8.3-safe | Check `mdir -i /dos/c/dos.img@@$((62*512)) ::SERDFS/DOS/BUILD`; re-run `_install_dos.sh`. |
| `86box-cmd: timeout after Ns` | The BAT hung past the timeout (default 60 s) | Inspect `mtype -i /dos/c/AGENT.IMG ::OUT.TXT` to see how far it got. Increase via `--timeout 240`. |
| Blank output (just `__86BOX_AGENT_DONE__`) | Program ran but wrote nothing to stdout | DOS COPY/TYPE failures go to stderr (not redirected). Use `MEM /C` after install to confirm TSR is loaded. |
| Bytes look corrupted in DOS-side output | Status-bar redraw escape sequences interleaved with the install banner | Use `grep -aF` (binary-safe + literal) in test asserts. The functional output is still in there. |

The `__86BOX_AGENT_DONE__` marker proves the BAT reached its end. If
you see it, the program really did exit (whether or not it produced
output).

## Starting the COM1 PTY serial bridge

```bash
86box-bridge                  # default: --port 1, --link /tmp/linux-com1
```

`86box-bridge` polls the 86Box log for the `serial_passthrough: Slave
side is /dev/pts/N` line, opens that PTY raw, creates an intermediate
raw PTY pair under our control, symlinks `/tmp/linux-com1` → the new
slave, and shuttles bytes both ways with a 4 ms/byte host→86Box
throttle (necessary on 86Box+QEMU under Apple Silicon — without it
sustained transfers drop bytes).

Verify it's up:

```bash
86box-bridge status
ls -l /tmp/linux-com1         # → /dev/pts/N
```

For `86box-pcmd`, the bridge also handles COM2 with `--port 2 --link
/tmp/linux-com2`. They coexist.

## Starting the Linux daemon

```bash
cd /dos/c/serdfs       # required: package layout for `python3 -m linux.serdfsd`
python3 -m linux.serdfsd \
    --serial /tmp/linux-com1 \
    --baud 9600 \
    --root /tmp/serdfs-testroot \
    --name-mode compat \
    --log-level INFO
```

Use `--log-level DEBUG` to see every `rx cmd=0xNN seq=N plen=N` request
plus the `LIST_DIR_BEGIN path=`, `OPEN mode=N path=`, etc. Per-request
log lines go to stderr; redirect with `2>/tmp/d.log`.

Exits cleanly on SIGTERM / SIGINT.

## Running tests

```bash
# Python unit tests — uses bundled mini-runner (no pytest needed)
cd /dos/c/serdfs
python3 linux/serdfsd/tests/test_runner.py linux/serdfsd/tests/*.py

# Phase 1 dev-environment smoke
bash tests/e2e/smoke.sh

# All Phase 2+ e2e tests
bash -c 'for s in tests/e2e/e2e_*.sh tests/e2e/fault_inject.sh; do
             bash "$s" || exit 1
         done'
```

Current end-to-end status (sandbox):

| Test | Result | Notes |
|---|---|---|
| Python unit tests | 100/100 PASS | `test_fsops_readonly`, `test_fsops_write`, `test_namemap`, `test_pathjail`, `test_frame` |
| `e2e_ping.sh` | 3/3 PASS | SERPING round-trip via SerialDFS frame protocol |
| `e2e_tsr.sh` | 9/9 PASS | install + DIR + TYPE + MKDIR + 2nd DIR + /STATS + /U |
| `e2e_copy.sh` | 6/6 PASS | 8 KB binary round-trip with byte-for-byte cmp |
| `fault_inject.sh` | 6/6 PASS | daemon killed mid-test → /STATS timeouts, dest absent |

## Diagnostic checklist when something breaks

1. `86box-run status` — Xvfb / x11vnc / 86Box up?
2. `tail /tmp/86box/86box.log` — Qt + emulator stderr.
3. `86box-bridge status` — bridge alive? Symlink target valid?
4. `cat /tmp/86box/bridge.log` — bridge stdout (last "shuttle ended"
   means 86Box closed its PTY).
5. `mdir -i /dos/c/AGENT.IMG ::` — was OUT.TXT produced? `mtype -i
   /dos/c/AGENT.IMG ::OUT.TXT` to read it raw without 86box-cmd's
   timeout-truncated stdout.
6. `vncdo -s ::5901 capture /tmp/peek.png` and Read it to see the
   actual DOS screen state if the text scraper is unhappy.
7. For serial-byte-corruption suspicions: `BOX86_BRIDGE_TRACE=1 86box-bridge
   foreground` (after `86box-bridge stop`) — hex-dumps every chunk
   both ways.

For dev-environment-side issues (tooling, not SerialDFS itself), the
canonical reference is `/workspace/tools/86box/README.md` + the gotcha
table at the bottom.

## Phase discipline reminder (R15)

Work one phase at a time. No Phase N+1 code until Phase N verification
passes and `TODO_TRACKER.md` is updated. Each completed phase gets a
git commit. As of 2026-04-27 all v1 phases are closed (Phase 11 / real
hardware deferred); see TODO_TRACKER for the current state.

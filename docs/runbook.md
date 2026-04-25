# SerialDFS Runbook

## Building DOS binaries

```bash
cd /dos/c/serdfs/dos
wmake hello        # build HELLO.EXE smoke pin
wmake all          # build all DOS .EXE targets
```

Note: the container has `wmake` (OpenWatcom), not GNU `make`. Use `wmake` for
all DOS-side builds.

## Running DOS commands in DOSBox

```bash
dosbox-cmd 'C:\SERDFS\DOS\BUILD\HELLO.EXE'
dosbox-cmd 'dir c:'
dosbox-cmd 'dir d:'
```

DOSBox mounts:
- `C:` → `/dos/c`
- `D:` → `/dos/src`

All DOS paths must be 8.3-safe. The project root is `C:\SERDFS\` (6 chars).

## dosbox-cmd output capture — failure modes (R4)

`dosbox-cmd` routes DOS stdout to `C:\AGENT\OUT.TXT` via a generated batch
file, then reads it back. There are several ways output capture can fail that
do NOT mean your program crashed:

| Symptom | What it means | What to check |
|---|---|---|
| `Bad command or filename - "X"` | Path does not exist or is not 8.3-safe | Check the path; verify file was built |
| `No DOS output captured` | DOSBox launched but OUT.TXT empty | Check `/dos/c/AGENT/RUN.BAT`, `/dos/c/AGENT/OUT.TXT`; try a known-good command |
| Blank output (only `__DOSBOX_AGENT_DONE__`) | Program ran but wrote nothing to stdout | Program may write directly to screen (can't be captured); add explicit `printf` |
| Timeout with no output | DOSBox did not finish in 20 s | Check for infinite loop or hung program |

Verified failure shape (Phase 1 baseline):
```
Bad command or filename - "blibble"
__DOSBOX_AGENT_DONE__
```
This is a successful DOSBox run — the program simply does not exist.
Exit code from `dosbox-cmd` is 0 in this case; the error text is the only signal.
Always `grep` for expected output rather than relying on exit code.

## Starting the PTY serial bridge (Phase 2+)

```bash
bash linux/ptybridge/start_pty_bridge.sh
```

Verify both ends exist before running serial tests:
```bash
ls -l /tmp/dosbox-com1 /tmp/linux-com1
```

## Starting the Linux daemon (Phase 4+)

```bash
python3 -m linux.serdfsd \
  --serial /tmp/linux-com1 \
  --baud 9600 \
  --root /tmp/serdfs-testroot \
  --name-mode compat \
  --log-level debug
```

## Running tests

```bash
# Python unit tests (Phase 3+)
cd /dos/c/serdfs
python3 -m pytest linux/serdfsd/tests/ -v

# End-to-end smoke (Phase 1)
bash tests/e2e/smoke.sh

# All e2e tests (Phase 9+)
bash -c 'for s in tests/e2e/*.sh; do bash "$s" || exit 1; done'
```

## Phase discipline reminder (R15)

Work one phase at a time. No Phase N+1 code until Phase N verification passes
and `TODO_TRACKER.md` is updated. Each completed phase gets a git commit.

# SerialDFS

DOS 6.22 / 80286 remote filesystem over a serial port.

A small TSR (`SERDFS.EXE`) runs on the DOS machine and exposes a mapped drive
letter backed by a Linux/Pi daemon (`serdfsd`). All network work — SMB mounts,
long-name aliasing, caching, retries — happens on the Linux side. The DOS machine
needs nothing except a COM port.

## Requirements

**DOS side:**
- DOS 6.22, 80286-class PC, ~1 MB RAM
- COM1 or COM2 serial port

**Linux/Pi side:**
- Python 3.8+ with pyserial
- Serial device (`/dev/ttyUSB0` or equivalent)
- Backend root: local directory or SMB-mounted share

**Development (container):**
- Open Watcom 2.0 (`wcl`) for DOS cross-compilation
- DOSBox-X with `directserial` PTY bridge
- `socat` for PTY bridge setup

## Quickstart

```bash
# build DOS tools
make hello          # smoke pin: HELLO.EXE
make dos            # all DOS .EXE targets

# start daemon
python -m serdfsd --serial /tmp/linux-com1 --baud 9600 --root /tmp/testroot

# mount drive in DOSBox
dosbox-cmd "C:\SERDFS\DOS\BUILD\SERDFS.EXE X: /COM1 /BAUD:9600 /NOSTATUS"
dosbox-cmd "DIR X:"
```

## Repository Layout

```
protocol/       wire format spec, CRC reference, golden test vectors
dos/            DOS client: TSR source, tools, tests, build output
linux/          Linux daemon: serdfsd (Python), PTY bridge, echo server
docs/           architecture decision records, runbook, etherdfs notes
tests/e2e/      cross-component end-to-end test scripts
vendor/         frozen EtherDFS baseline snapshot (GPL attribution)
```

## Implementation Phases

See `TODO_TRACKER.md` for live phase status.
Spec: `../etherdfs/SERIALDFS_SPEC.md`
Risks: `../etherdfs/SERIALDFS_IMPLEMENTATION_RISKS.md`

## License

GPL-2.0 — see `COPYING`. Derivative of EtherDFS by Mateusz Viste — see `NOTICES`.

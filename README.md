# SerialDFS

DOS 6.22 / 80286 remote filesystem over a serial port.

A small TSR (`SERDFS.EXE`) runs on the DOS machine and exposes a mapped
drive letter backed by a Linux/Pi daemon (`serdfsd`). All network work —
SMB mounts, long-name aliasing, caching, retries — happens on the Linux
side. The DOS machine needs nothing except a COM port.

## Requirements

**DOS side:**
- DOS 6.22, 80286-class PC, ~1 MB RAM
- COM1 or COM2 serial port (8250 or 16550)
- `LASTDRIVE` in `CONFIG.SYS` reaching the chosen mapped drive letter

**Linux/Pi side:**
- Python 3.8+ with pyserial
- Serial device (`/dev/ttyUSB0` on real hardware, `/tmp/linux-com1` PTY
  in the sandbox via `86box-bridge`)
- Backend root: local directory or SMB-mounted share

**Development (sandbox container):**
- The 86Box + Open Watcom dev environment under `/workspace/` (see
  `/workspace/README.md` and `/workspace/AGENT.md` for the toolkit);
  this project is meant to be cloned into a project's `dos-c/` mount.
- Toolchain: Open Watcom 2.0 (`wcl`), 86Box v5.3, `serdfsd` Python
  daemon, `86box-bridge` for COM1/COM2 PTY plumbing, `86box-cmd` /
  `86box-pcmd` for driving DOS.

## Quickstart

```bash
# 1. build DOS-side binaries (uses Open Watcom)
make dos                           # → dos/build/*.EXE

# 2. install the binaries into the DOS C: drive
86box-run stop                     # don't write dos.img while 86Box is up
bash tests/e2e/_install_dos.sh     # mcopies dos/build/*.EXE → C:\SERDFS\DOS\BUILD\

# 3. bring up the COM1 PTY bridge + daemon
86box-bridge                       # publishes /tmp/linux-com1
( cd . && python3 -m linux.serdfsd \
      --serial /tmp/linux-com1 --baud 9600 \
      --root /tmp/serdfs-testroot --log-level INFO ) &

# 4. install the TSR + try a directory listing — all in one BAT/cold-boot
86box-cmd <<'BAT'
C:\SERDFS\DOS\BUILD\SERDFS.EXE C-X /COM1 /BAUD:9600 /NOSTATUS
DIR X:\
TYPE X:\HELLO.TXT
C:\SERDFS\DOS\BUILD\SERDFS.EXE /STATS
C:\SERDFS\DOS\BUILD\SERDFS.EXE /U
BAT
```

For the full per-tool reference (every `.EXE`'s flags, exit codes,
examples) see [`docs/USERMANUAL.md`](docs/USERMANUAL.md).
For the operational walkthrough (what to run when, common failure
modes, how to inspect dos.img) see [`docs/runbook.md`](docs/runbook.md).

## Repository layout

```
protocol/       wire format spec, CRC reference, golden test vectors
dos/            DOS client: TSR source, tools, build output, makefile
  src/          DGROUP/BEGTEXT TSR sources, seruart/serframe/serrpc, dosstruc.h
  tools/        non-resident DOS tools (SERDIR, SERTYPE, SERPING, …)
  build/        wmake outputs (.EXE files; gitignored)
linux/          Linux daemon: serdfsd (Python), PTY bridge, echo server
  serdfsd/      transport.py + frame.py + fsops.py + namemap.py + handles.py
  ptybridge/    bridge launchers (start_86box_bridge.sh wrapping 86box-bridge)
  echo/         protocol echo server + test client
docs/           USERMANUAL.md, runbook.md, ADRs, etherdfs-notes.md
tests/e2e/      cross-component end-to-end test scripts (PASS suite)
vendor/         frozen EtherDFS baseline snapshot (GPL attribution)
```

## Status

- **All v1 phases ✅ Done in the sandbox.** Phase 11 (real hardware
  validation) is the only remaining checklist item; it requires a 286/386
  + RS-232 + Pi setup nobody in this container has.
- **Test suite:** 100/100 unit tests, e2e_ping 3/3, e2e_tsr 9/9,
  e2e_copy 6/6, fault_inject 6/6.
- **Resident size:** SERDFS TSR is ~11 KB on DOS 6.22 — well under the
  64 KB DoD cap.

See [`TODO_TRACKER.md`](TODO_TRACKER.md) for live phase status,
discoveries (D1–D8), and assumptions.

## Spec + risks

- Wire protocol: [`protocol/spec.txt`](protocol/spec.txt) (locked v1).
- Implementation risks register: `../etherdfs/SERIALDFS_IMPLEMENTATION_RISKS.md`.
- Architecture decisions: [`docs/adr/`](docs/adr/).

## License

GPL-2.0 — see `COPYING`. Derivative of EtherDFS by Mateusz Viste —
see `NOTICES`.

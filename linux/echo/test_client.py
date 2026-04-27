#!/usr/bin/env python3
"""
test_client.py — Integration test client for the SerialDFS daemon.
Starts the daemon as a subprocess on its own PTY, then drives it as if
it were a DOS client, printing PASS/FAIL for each operation.

Usage: python3 test_client.py --root <backend_dir> [--baud N]
"""
from __future__ import annotations
import argparse
import os
import pty
import struct
import subprocess
import sys
import time
import termios
import tty

# Allow importing serdfsd package
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))
from linux.serdfsd import frame as F

PASS = 0
FAIL = 0


def check(desc: str, cond: bool, detail: str = '') -> None:
    global PASS, FAIL
    if cond:
        print(f'  PASS: {desc}')
        PASS += 1
    else:
        print(f'  FAIL: {desc}' + (f' — {detail}' if detail else ''))
        FAIL += 1


def send(master_fd: int, cmd: int, seq: int, payload: bytes = b'') -> None:
    raw = F.encode(cmd, seq, payload=payload)
    os.write(master_fd, raw)


def recv(master_fd: int, timeout: float = 3.0) -> tuple | None:
    """Read one frame from master_fd with timeout. Returns decoded tuple or None."""
    import select
    buf = b''
    deadline = time.time() + timeout

    while time.time() < deadline:
        remaining = deadline - time.time()
        r, _, _ = select.select([master_fd], [], [], min(0.1, remaining))
        if not r:
            continue
        chunk = os.read(master_fd, 256)
        if not chunk:
            break
        buf += chunk
        # Try to decode
        if len(buf) >= F.HDR_SIZE:
            plen = struct.unpack_from('<H', buf, 8)[0]
            total = F.HDR_SIZE + plen + F.CRC_SIZE
            if len(buf) >= total:
                try:
                    return F.decode(buf[:total])
                except F.FrameError as e:
                    print(f'  frame error: {e}', file=sys.stderr)
                    return None
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', required=True)
    parser.add_argument('--baud', type=int, default=9600)
    args = parser.parse_args()

    root = args.root

    # Create a PTY pair for daemon↔client communication
    master_fd, slave_fd = pty.openpty()
    slave_path = os.ttyname(slave_fd)

    # Put master into raw mode
    attrs = termios.tcgetattr(master_fd)
    tty.setraw(master_fd)

    print(f'=== SerialDFS integration test client ===')
    print(f'root={root}  device={slave_path}  baud={args.baud}')

    # Start daemon as subprocess on the slave PTY
    daemon_cmd = [
        sys.executable, '-m', 'linux.serdfsd.serdfsd',
        '--serial', slave_path,
        '--baud',   str(args.baud),
        '--root',   root,
        '--log-level', 'WARNING',
        '--rebuild-namemap',
    ]
    proc = subprocess.Popen(
        daemon_cmd,
        cwd=os.path.join(os.path.dirname(__file__), '..', '..'),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    time.sleep(1.5)   # let daemon import modules and open the serial port

    if proc.poll() is not None:
        err = proc.stderr.read().decode(errors='replace')
        print(f'Daemon exited early: {err}', file=sys.stderr)
        return 1

    seq = [1]

    def rpc(cmd: int, payload: bytes = b'') -> tuple | None:
        s = seq[0]; seq[0] = (seq[0] + 1) & 0xFF or 1
        send(master_fd, cmd, s, payload)
        return recv(master_fd)

    try:
        # ── PING (retry up to 3× to absorb startup jitter) ────────────────────
        resp = None
        for _attempt in range(3):
            resp = rpc(F.CMD_PING)
            if resp is not None:
                break
            time.sleep(0.5)
        check('PING response received', resp is not None)
        if resp:
            cmd, rseq, _, status, _ = resp
            check('PING cmd=PING',    cmd == F.CMD_PING)
            check('PING status=OK',   status == F.STATUS_OK)

        # ── GET_INFO ──────────────────────────────────────────────────────────
        resp = rpc(F.CMD_GET_INFO)
        check('GET_INFO response', resp is not None)
        if resp:
            _, _, _, status, payload = resp
            check('GET_INFO status=OK', status == F.STATUS_OK)
            check('GET_INFO payload 4 bytes', len(payload) == 4)

        # ── LIST_DIR_BEGIN ────────────────────────────────────────────────────
        list_payload = b'X\\' + b'\x00'
        resp = rpc(F.CMD_LIST_DIR, list_payload)
        check('LIST_DIR response', resp is not None)
        names = set()
        if resp:
            _, _, _, status, payload = resp
            check('LIST_DIR status=OK', status == F.STATUS_OK)
            # payload[0] = handle ID; entries follow
            entries_data = payload[1:] if payload else b''
            for i in range(len(entries_data) // 23):
                entry = entries_data[i * 23:(i + 1) * 23]
                name = entry[:12].rstrip(b'\x00').decode('ascii', errors='replace')
                names.add(name.upper())
            check('LIST_DIR has entries', len(names) > 0, f'names={names}')

        # Check for README.TXT in listing
        readme_alias = next(
            (n for n in names if n.startswith('README')), None
        )
        check('README.TXT in listing', readme_alias is not None, f'names={names}')

        # ── OPEN / READ / CLOSE ───────────────────────────────────────────────
        if readme_alias:
            open_payload = bytes([0]) + f'\\{readme_alias}'.encode('ascii') + b'\x00'
            resp = rpc(F.CMD_OPEN, open_payload)
            check('OPEN README response', resp is not None)
            hid = None
            if resp:
                _, _, _, status, payload = resp
                check('OPEN status=OK', status == F.STATUS_OK,
                      f'status={status}')
                if status == F.STATUS_OK and payload:
                    hid = payload[0]

            if hid is not None:
                read_payload = struct.pack('<BH', hid, 512)
                resp = rpc(F.CMD_READ, read_payload)
                check('READ response', resp is not None)
                if resp:
                    _, _, _, status, data = resp
                    check('READ status=OK', status == F.STATUS_OK)
                    check('READ data non-empty', len(data) > 0, f'len={len(data)}')

                close_payload = bytes([hid])
                resp = rpc(F.CMD_CLOSE, close_payload)
                check('CLOSE response', resp is not None)
                if resp:
                    _, _, _, status, _ = resp
                    check('CLOSE status=OK', status == F.STATUS_OK)

        # ── Multi-chunk READ of BIGFILE.TXT (>512 bytes) ─────────────────────
        bigfile_alias = next(
            (n for n in names if n.startswith('BIGFILE')), None
        )
        check('BIGFILE.TXT in listing', bigfile_alias is not None, f'names={names}')
        if bigfile_alias:
            open_payload = bytes([0]) + f'\\{bigfile_alias}'.encode('ascii') + b'\x00'
            resp = rpc(F.CMD_OPEN, open_payload)
            check('OPEN BIGFILE response', resp is not None)
            big_hid = None
            if resp:
                _, _, _, status, payload = resp
                check('OPEN BIGFILE status=OK', status == F.STATUS_OK,
                      f'status={status}')
                if status == F.STATUS_OK and payload:
                    big_hid = payload[0]

            if big_hid is not None:
                total_bytes = 0
                chunks = 0
                eof = False
                while not eof:
                    read_payload = struct.pack('<BH', big_hid, 512)
                    resp = rpc(F.CMD_READ, read_payload)
                    if resp is None:
                        check('BIGFILE READ no timeout', False, 'recv returned None')
                        break
                    _, _, _, status, data = resp
                    if status != F.STATUS_OK:
                        check('BIGFILE READ status=OK', False, f'status={status}')
                        break
                    if len(data) == 0:
                        eof = True
                    else:
                        total_bytes += len(data)
                        chunks += 1
                check('BIGFILE multi-chunk read >1 chunk', chunks > 1,
                      f'chunks={chunks}')
                check('BIGFILE total bytes >512', total_bytes > 512,
                      f'total={total_bytes}')

                close_payload = bytes([big_hid])
                resp = rpc(F.CMD_CLOSE, close_payload)
                check('CLOSE BIGFILE response', resp is not None)
                if resp:
                    _, _, _, status, _ = resp
                    check('CLOSE BIGFILE status=OK', status == F.STATUS_OK)

        # ── ERR_BAD_PATH (V4) ─────────────────────────────────────────────────
        bad_payload = bytes([0]) + b'\\..\\SECRET.TXT\x00'
        resp = rpc(F.CMD_OPEN, bad_payload)
        check('ERR_BAD_PATH response', resp is not None)
        if resp:
            _, _, _, status, _ = resp
            check('OPEN traversal → ERR_BAD_PATH',
                  status == F.ERR_BAD_PATH, f'status={status}')

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
        os.close(master_fd)
        os.close(slave_fd)

    print()
    print(f'Results: {PASS} passed, {FAIL} failed')
    return 0 if FAIL == 0 else 1


if __name__ == '__main__':
    sys.exit(main())

"""
serdfsd.py — SerialDFS daemon entry point.

Usage:
  python3 -m serdfsd --serial /tmp/linux-com1 --baud 9600 --root /path/to/root

The daemon opens the serial device, processes incoming SerialDFS frames,
and performs filesystem operations against --root.
"""
from __future__ import annotations
import argparse
import logging
import os
import signal
import sys
from pathlib import Path

from . import frame as F
from .transport import SerialTransport
from .namemap import NameMap, NameMapError
from .handles import HandleTable
from . import fsops

log = logging.getLogger('serdfsd')


def parse_args(argv=None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        prog='serdfsd',
        description='SerialDFS daemon — serves filesystem RPCs over serial.',
    )
    p.add_argument('--serial',   default='/tmp/linux-com1',
                   help='Serial device path (default: /tmp/linux-com1)')
    p.add_argument('--baud',     type=int, default=9600,
                   help='Baud rate (default: 9600)')
    p.add_argument('--root',     required=True,
                   help='Backend root directory to serve')
    p.add_argument('--name-mode', default='compat',
                   choices=['compat', 'strict-prefix'],
                   help='8.3 alias generation mode (default: compat)')
    p.add_argument('--log-level', default='INFO',
                   choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'],
                   help='Logging level (default: INFO)')
    p.add_argument('--rebuild-namemap', action='store_true',
                   help='Ignore corrupt namemap and rebuild from scratch')
    return p.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)

    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format='%(asctime)s %(name)s %(levelname)s %(message)s',
        stream=sys.stderr,
    )

    root = Path(args.root)
    if not root.is_dir():
        log.error('root directory does not exist: %s', root)
        return 1

    # Load (or rebuild) namemap — fails-closed on corruption unless --rebuild-namemap
    try:
        namemap = NameMap(root, mode=args.name_mode, rebuild=args.rebuild_namemap)
    except NameMapError as e:
        log.error('%s', e)
        return 1

    handles = HandleTable()

    # Open serial transport
    try:
        transport = SerialTransport(args.serial, args.baud)
    except Exception as e:
        log.error('cannot open serial device %s: %s', args.serial, e)
        return 1

    log.info('serdfsd ready: root=%s serial=%s baud=%d', root, args.serial, args.baud)

    # Graceful shutdown on SIGTERM/SIGINT
    _running = [True]

    def _stop(sig, frame):
        log.info('shutting down (signal %d)', sig)
        _running[0] = False

    signal.signal(signal.SIGTERM, _stop)
    signal.signal(signal.SIGINT,  _stop)

    # ── Main loop ─────────────────────────────────────────────────────────────
    while _running[0]:
        try:
            raw = transport.read_frame()
        except F.ShortFrame as e:
            log.debug('short/timeout: %s', e)
            continue
        except Exception as e:
            log.warning('read error: %s', e)
            continue

        try:
            cmd, seq, flags, _status, payload = F.decode(raw)
        except F.BadCRC as e:
            log.warning('CRC error: %s', e)
            # Send an ERR_CRC reply with seq=0 (best-effort; we may not know seq)
            _send_error(transport, F.CMD_PING, 0, F.ERR_CRC)
            continue
        except F.FrameError as e:
            log.warning('frame error: %s', e)
            continue

        log.debug('rx cmd=0x%02x seq=%d plen=%d', cmd, seq, len(payload))

        status, resp_payload = fsops.dispatch(cmd, seq, payload, root, namemap, handles)

        resp = F.encode(cmd, seq, payload=resp_payload, status=status)
        try:
            transport.write_frame(resp)
        except Exception as e:
            log.warning('write error: %s', e)

    transport.close()
    namemap.save_if_dirty()
    log.info('serdfsd stopped')
    return 0


def _send_error(transport: SerialTransport, cmd: int, seq: int, status: int) -> None:
    try:
        transport.write_frame(F.encode(cmd, seq, status=status))
    except Exception:
        pass


if __name__ == '__main__':
    sys.exit(main())

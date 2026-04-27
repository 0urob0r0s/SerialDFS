#!/usr/bin/env python3
"""
Phase 2/3 echo server.
Speaks the SerialDFS frame protocol: reads PING frames, responds with PONG.
Falls back to raw-byte echo only when --raw flag is given (legacy Phase 2 mode).

Usage: python3 echo_server.py <device> <baud> [--raw]
"""
import sys
import os
import struct

# Allow running from any directory
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

import serial
from linux.serdfsd import frame as F


def serve_framed(ser):
    """Read SerialDFS frames and reply to PING with PONG."""
    print(f"Frame-protocol echo on {ser.name} at {ser.baudrate} baud.  Ctrl-C to stop.",
          flush=True)
    rxbuf = b''
    while True:
        chunk = ser.read(256)
        if not chunk:
            continue
        rxbuf += chunk
        # Try to decode a complete frame
        while len(rxbuf) >= F.MIN_SIZE:
            # Peek at payload_len to know total frame size
            if rxbuf[:2] != F.MAGIC:
                # Resync: discard bytes until we see 'SD' magic
                idx = rxbuf.find(F.MAGIC, 1)
                if idx < 0:
                    rxbuf = b''
                else:
                    rxbuf = rxbuf[idx:]
                break
            if len(rxbuf) < F.HDR_SIZE:
                break
            plen = struct.unpack_from('<H', rxbuf, 8)[0]
            total = F.HDR_SIZE + plen + F.CRC_SIZE
            if len(rxbuf) < total:
                break
            raw_frame = rxbuf[:total]
            rxbuf = rxbuf[total:]
            try:
                cmd, seq, flags, status, payload = F.decode(raw_frame)
            except F.BadCRC as e:
                print(f"  bad CRC: {e}", flush=True)
                resp = F.encode(cmd if 'cmd' in dir() else 0x01, seq if 'seq' in dir() else 0,
                                status=F.ERR_CRC)
                ser.write(resp)
                ser.flush()
                continue
            except F.FrameError as e:
                print(f"  frame error: {e}", flush=True)
                continue

            print(f"  rx cmd=0x{cmd:02x} seq={seq} plen={len(payload)}", flush=True)
            if cmd == F.CMD_PING:
                resp = F.encode(F.CMD_PING, seq, status=F.STATUS_OK)
                ser.write(resp)
                ser.flush()
                print(f"  tx PONG seq={seq} ({len(resp)} bytes)", flush=True)
            else:
                resp = F.encode(cmd, seq, status=F.ERR_UNSUPPORTED)
                ser.write(resp)
                ser.flush()
                print(f"  tx ERR_UNSUPPORTED cmd=0x{cmd:02x}", flush=True)


def serve_raw(ser):
    """Legacy: echo raw bytes verbatim (Phase 2 byte-level test only)."""
    print(f"Raw-byte echo on {ser.name} at {ser.baudrate} baud.  Ctrl-C to stop.",
          flush=True)
    while True:
        data = ser.read(256)
        if data:
            ser.write(data)
            ser.flush()
            print(f"  echoed {len(data)} byte(s): {data.hex()}", flush=True)


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <device> <baud> [--raw]", file=sys.stderr)
        sys.exit(1)
    device = sys.argv[1]
    baud   = int(sys.argv[2])
    raw    = '--raw' in sys.argv

    with serial.Serial(device, baud, timeout=0.1) as ser:
        if raw:
            serve_raw(ser)
        else:
            serve_framed(ser)


if __name__ == '__main__':
    main()

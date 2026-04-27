"""
transport.py — SerialTransport: serial I/O with frame resync.

read_frame():
  - Reads the 10-byte header, peeks at payload_len, reads payload + CRC.
  - If magic bytes are wrong, discards one byte at a time until 'SD' is found
    (partial resync for garbage recovery).
  - Raises frame.ShortFrame on timeout before a complete frame arrives.

write_frame(data):
  - Writes the raw frame bytes to the serial device.
"""
from __future__ import annotations
import logging
import struct
import serial

from . import frame as F

log = logging.getLogger(__name__)

RESYNC_MAX = 4096   # discard at most this many garbage bytes before giving up


class SerialTransport:
    def __init__(self, device: str, baud: int, timeout: float = 2.0):
        self._ser = serial.Serial(device, baud, timeout=timeout)
        log.info('transport open: %s @ %d baud', device, baud)

    def close(self) -> None:
        self._ser.close()

    def read_frame(self) -> bytes:
        """
        Block until one complete valid frame is available, then return its
        raw bytes (suitable for passing to frame.decode()).
        Raises frame.ShortFrame if the serial port times out mid-frame.
        """
        # ── Step 1: resync to magic 'SD' ─────────────────────────────────────
        discarded = 0
        while True:
            b0 = self._ser.read(1)
            if not b0:
                raise F.ShortFrame("timeout waiting for frame magic byte 0")
            if b0 != b'S':
                discarded += 1
                if discarded >= RESYNC_MAX:
                    raise F.ShortFrame("too many garbage bytes before magic")
                continue
            b1 = self._ser.read(1)
            if not b1:
                raise F.ShortFrame("timeout waiting for frame magic byte 1")
            if b1 == b'D':
                break   # found 'SD'
            # b1 != 'D': b1 might itself be 'S'
            if b1 == b'S':
                # Don't discard b1; loop will re-check it as b0 next pass
                # by peeking — simplest: just push b1 back by looping
                discarded += 1
                continue
            discarded += 1

        if discarded:
            log.warning('resynced after %d garbage byte(s)', discarded)

        # ── Step 2: read the rest of the header (8 bytes) ────────────────────
        rest_hdr = self._ser.read(F.HDR_SIZE - 2)
        if len(rest_hdr) < F.HDR_SIZE - 2:
            raise F.ShortFrame("timeout reading frame header")
        hdr = b'SD' + rest_hdr

        # peek payload_len (little-endian uint16 at offset 8)
        plen = struct.unpack_from('<H', hdr, 8)[0]
        if plen > F.MAX_PAYLOAD:
            # Garbled — treat as resync needed; caller handles the error
            raise F.ShortFrame(f"payload_len {plen} exceeds max {F.MAX_PAYLOAD}")

        # ── Step 3: read payload + CRC ────────────────────────────────────────
        tail_len = plen + F.CRC_SIZE
        tail = self._ser.read(tail_len)
        if len(tail) < tail_len:
            raise F.ShortFrame("timeout reading frame body/CRC")

        return hdr + tail

    def write_frame(self, data: bytes) -> None:
        self._ser.write(data)
        self._ser.flush()

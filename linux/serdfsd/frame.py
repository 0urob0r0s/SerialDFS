"""
frame.py — SerialDFS frame codec.
Python twin of dos/src/serframe.c.  Both must produce identical output
for every byte of the conformance vectors in protocol/vectors/.

CRC variant (locked, ADR-0003):
  CRC-16/CCITT-FALSE  poly=0x1021  init=0xFFFF  refin=No  refout=No  xorout=0
  check value 0x29B1 = CRC of b'123456789'
"""
import struct

# ── Frame constants ──────────────────────────────────────────────────────────
MAGIC       = b'SD'
VERSION     = 1
HDR_SIZE    = 10
CRC_SIZE    = 2
MIN_SIZE    = HDR_SIZE + CRC_SIZE   # 12
MAX_PAYLOAD = 512
MAX_SIZE    = HDR_SIZE + MAX_PAYLOAD + CRC_SIZE  # 524

# ── Command IDs (spec §3) ────────────────────────────────────────────────────
CMD_PING           = 0x01
CMD_GET_INFO       = 0x02
CMD_LIST_DIR       = 0x10
CMD_LIST_DIR_NEXT  = 0x11
CMD_OPEN           = 0x20
CMD_CREATE         = 0x21
CMD_READ           = 0x22
CMD_WRITE          = 0x23
CMD_SEEK           = 0x24
CMD_CLOSE          = 0x25
CMD_DELETE         = 0x30
CMD_RENAME         = 0x31
CMD_MKDIR          = 0x32
CMD_RMDIR          = 0x33
CMD_GET_ATTR       = 0x40
CMD_SET_ATTR       = 0x41
CMD_GET_TIME       = 0x42
CMD_SET_TIME       = 0x43
CMD_FLUSH          = 0x50
CMD_STATUS         = 0x60

# ── Status IDs (spec §4) ─────────────────────────────────────────────────────
STATUS_OK          = 0x00
ERR_NOT_FOUND      = 0x01
ERR_ACCESS         = 0x02
ERR_BAD_HANDLE     = 0x03
ERR_EXISTS         = 0x04
ERR_IO             = 0x05
ERR_CRC            = 0x06
ERR_TIMEOUT        = 0x07
ERR_UNSUPPORTED    = 0x08
ERR_BAD_PATH       = 0x09
ERR_PROTOCOL       = 0x0A

# ── Flag bits (spec §5) ──────────────────────────────────────────────────────
FLAG_NONE  = 0x00
FLAG_RETRY = 0x01

# ── CRC table ────────────────────────────────────────────────────────────────
def _build_table() -> list:
    t = []
    for i in range(256):
        v = i << 8
        for _ in range(8):
            v = ((v << 1) ^ 0x1021) & 0xFFFF if v & 0x8000 else (v << 1) & 0xFFFF
        t.append(v)
    return t

_CRC_TABLE = _build_table()


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE of data."""
    crc = 0xFFFF
    for b in data:
        crc = (_CRC_TABLE[((crc >> 8) ^ b) & 0xFF] ^ (crc << 8)) & 0xFFFF
    return crc


# ── Exceptions ───────────────────────────────────────────────────────────────
class FrameError(Exception):
    pass

class ShortFrame(FrameError):
    pass

class BadMagic(FrameError):
    pass

class BadVersion(FrameError):
    pass

class BadCRC(FrameError):
    pass

class PayloadTooLong(FrameError):
    pass


# ── Public API ───────────────────────────────────────────────────────────────

def encode(cmd: int, seq: int, payload: bytes = b'',
           flags: int = FLAG_NONE, status: int = STATUS_OK) -> bytes:
    """
    Encode a frame.  Returns complete frame bytes including CRC.
    Raises PayloadTooLong if len(payload) > MAX_PAYLOAD.
    """
    if len(payload) > MAX_PAYLOAD:
        raise PayloadTooLong(f"payload {len(payload)} > {MAX_PAYLOAD}")
    header = struct.pack('<2sBBBBBBH',
                        MAGIC, VERSION, flags, seq, cmd, status, 0,
                        len(payload))
    body = header + payload
    return body + struct.pack('<H', crc16(body))


def decode(buf: bytes) -> tuple:
    """
    Decode a frame from buf.

    Returns (cmd, seq, flags, status, payload) on success.

    Raises:
      ShortFrame     — buf too short to contain a complete frame
      BadMagic       — first two bytes are not 'SD'
      BadVersion     — version field != 1
      PayloadTooLong — payload_len field > MAX_PAYLOAD
      BadCRC         — CRC mismatch
    """
    if len(buf) < MIN_SIZE:
        raise ShortFrame(f"need >={MIN_SIZE} bytes, got {len(buf)}")
    if buf[:2] != MAGIC:
        raise BadMagic(f"expected 5344, got {buf[:2].hex()}")
    version = buf[2]
    if version != VERSION:
        raise BadVersion(f"expected version {VERSION}, got {version}")
    flags   = buf[3]
    seq     = buf[4]
    cmd     = buf[5]
    status  = buf[6]
    # reserved = buf[7] — ignored on receive
    plen = struct.unpack_from('<H', buf, 8)[0]
    if plen > MAX_PAYLOAD:
        raise PayloadTooLong(f"payload_len={plen} > {MAX_PAYLOAD}")
    total = HDR_SIZE + plen + CRC_SIZE
    if len(buf) < total:
        raise ShortFrame(f"need {total} bytes, got {len(buf)}")
    payload  = bytes(buf[HDR_SIZE:HDR_SIZE + plen])
    crc_wire = struct.unpack_from('<H', buf, HDR_SIZE + plen)[0]
    crc_calc = crc16(buf[:HDR_SIZE + plen])
    if crc_wire != crc_calc:
        raise BadCRC(f"CRC wire={crc_wire:04x} calc={crc_calc:04x}")
    return cmd, seq, flags, status, payload


def read_frame(read_fn, timeout_secs: float = 2.0) -> bytes:
    """
    Read exactly one complete frame from read_fn(n) -> bytes.
    read_fn must block for up to timeout_secs and return 1..n bytes,
    or raise/return b'' on timeout.

    Returns raw frame bytes suitable for passing to decode().
    Raises ShortFrame on timeout before a complete frame arrives.
    """
    buf = b''
    # Read header first
    while len(buf) < HDR_SIZE:
        chunk = read_fn(HDR_SIZE - len(buf))
        if not chunk:
            raise ShortFrame("timeout reading frame header")
        buf += chunk
    plen = struct.unpack_from('<H', buf, 8)[0]
    if plen > MAX_PAYLOAD:
        raise PayloadTooLong(f"payload_len={plen} > {MAX_PAYLOAD}")
    tail = plen + CRC_SIZE
    while len(buf) < HDR_SIZE + tail:
        chunk = read_fn(HDR_SIZE + tail - len(buf))
        if not chunk:
            raise ShortFrame("timeout reading frame body")
        buf += chunk
    return buf

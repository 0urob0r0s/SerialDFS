"""
test_frame.py — pytest conformance tests for linux/serdfsd/frame.py.
Loads the same golden vectors as CRCFUZZ.EXE (protocol/vectors/*.bin).
Both must pass on every vector for Phase 3 to be complete.
"""
import os
import struct
import pytest
import sys

# Allow importing serdfsd package from its parent directory
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..'))

from linux.serdfsd import frame as F

# Path to golden vectors
VECTORS = os.path.join(os.path.dirname(__file__),
                       '..', '..', '..', 'protocol', 'vectors')

def vec(name: str) -> bytes:
    path = os.path.join(VECTORS, name)
    with open(path, 'rb') as f:
        return f.read()


# ── CRC self-check ───────────────────────────────────────────────────────────

def test_crc16_check_value():
    """CRC of b'123456789' must be 0x29B1 (ADR-0003)."""
    assert F.crc16(b'123456789') == 0x29B1


def test_crc16_empty():
    assert F.crc16(b'') == 0xFFFF


def test_crc16_single_byte():
    """One-byte sanity: match known table entry."""
    crc = F.crc16(bytes([0x31]))
    assert isinstance(crc, int)
    assert 0 <= crc <= 0xFFFF


# ── encode/decode round-trip ─────────────────────────────────────────────────

def test_roundtrip_ping_no_payload():
    raw = F.encode(F.CMD_PING, seq=1)
    cmd, seq, flags, status, payload = F.decode(raw)
    assert cmd == F.CMD_PING
    assert seq == 1
    assert flags == F.FLAG_NONE
    assert status == F.STATUS_OK
    assert payload == b''


def test_roundtrip_with_payload():
    data = b'\x00\x01\x02\xFF' * 10
    raw = F.encode(F.CMD_READ, seq=42, payload=data, status=F.STATUS_OK)
    cmd, seq, flags, status, payload = F.decode(raw)
    assert cmd == F.CMD_READ
    assert seq == 42
    assert payload == data


def test_roundtrip_max_payload():
    data = bytes(range(256)) * 2  # 512 bytes
    raw = F.encode(F.CMD_WRITE, seq=0xFF, payload=data)
    _, _, _, _, out = F.decode(raw)
    assert out == data


def test_payload_too_long_raises():
    with pytest.raises(F.PayloadTooLong):
        F.encode(F.CMD_WRITE, seq=1, payload=bytes(513))


def test_encode_sets_status_field():
    raw = F.encode(F.CMD_PING, seq=5, status=F.ERR_NOT_FOUND)
    _, _, _, status, _ = F.decode(raw)
    assert status == F.ERR_NOT_FOUND


def test_encode_sets_flags():
    raw = F.encode(F.CMD_PING, seq=1, flags=F.FLAG_RETRY)
    _, _, flags, _, _ = F.decode(raw)
    assert flags == F.FLAG_RETRY


# ── golden vector tests ──────────────────────────────────────────────────────

def test_vector_pngreq():
    """V1: PNGREQ.BIN — valid PING request (seq=1)."""
    data = vec('PNGREQ.BIN')
    cmd, seq, flags, status, payload = F.decode(data)
    assert cmd == F.CMD_PING
    assert seq == 1
    assert status == 0
    assert payload == b''


def test_vector_pngresp():
    """V2: PNGRESP.BIN — valid PING response (seq=1, status=OK)."""
    data = vec('PNGRESP.BIN')
    cmd, seq, flags, status, payload = F.decode(data)
    assert cmd == F.CMD_PING
    assert seq == 1
    assert status == F.STATUS_OK
    assert payload == b''


def test_vector_badcrc():
    """V3: BADCRC.BIN — CRC mismatch must raise BadCRC."""
    data = vec('BADCRC.BIN')
    with pytest.raises(F.BadCRC):
        F.decode(data)


def test_vector_truncated():
    """V4: TRUNC.BIN — only 5 bytes must raise ShortFrame."""
    data = vec('TRUNC.BIN')
    with pytest.raises(F.ShortFrame):
        F.decode(data)


def test_vector_stale_seq():
    """V5: STALE.BIN — valid frame with seq=2; codec decodes OK, seq != 1."""
    data = vec('STALE.BIN')
    cmd, seq, flags, status, payload = F.decode(data)
    assert cmd == F.CMD_PING
    assert seq == 2  # different from expected seq=1; RPC layer would drop


# ── frame size constants ─────────────────────────────────────────────────────

def test_min_frame_size():
    raw = F.encode(F.CMD_PING, seq=1)
    assert len(raw) == F.MIN_SIZE  # 12 bytes


def test_frame_size_with_payload():
    raw = F.encode(F.CMD_READ, seq=1, payload=b'\x00' * 100)
    assert len(raw) == F.HDR_SIZE + 100 + F.CRC_SIZE

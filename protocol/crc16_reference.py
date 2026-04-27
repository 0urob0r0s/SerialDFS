#!/usr/bin/env python3
"""
CRC-16/CCITT-FALSE reference implementation.
  poly   = 0x1021
  init   = 0xFFFF
  refin  = False
  refout = False
  xorout = 0x0000
  check  = 0x29B1  (CRC of ASCII "123456789")

This file is the canonical reference for ADR-0003.
Both serframe.c and frame.py must produce identical output.
"""


def _build_table():
    table = []
    for i in range(256):
        crc = i << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
        table.append(crc)
    return table


_TABLE = _build_table()


def crc16(data: bytes) -> int:
    """Return CRC-16/CCITT-FALSE of data."""
    crc = 0xFFFF
    for b in data:
        crc = (_TABLE[((crc >> 8) ^ b) & 0xFF] ^ (crc << 8)) & 0xFFFF
    return crc


if __name__ == '__main__':
    check = crc16(b'123456789')
    assert check == 0x29B1, f"self-check FAILED: got {check:#06x}"
    print(f"CRC of '123456789' = {check:#06x}  (expected 0x29b1)  OK")

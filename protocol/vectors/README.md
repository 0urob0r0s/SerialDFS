protocol/vectors/
==================
Golden test vectors for the SerialDFS frame codec.

Each .bin file is a raw binary frame or partial frame.
Each .hex.txt is a human-readable annotation of its fields.

  PNGREQ.BIN  — valid PING request  (seq=1)         FRAME_OK
  PNGRESP.BIN — valid PING response (seq=1, OK)      FRAME_OK
  BADCRC.BIN  — PING with corrupted CRC              FRAME_BADCRC
  TRUNC.BIN   — only first 5 bytes                   FRAME_SHORT
  STALE.BIN   — valid PING but seq=2 (wrong seq)     FRAME_OK (drop at RPC layer)

These vectors are consumed by:
  dos/tools/CRCFUZZ.EXE             (in-DOS conformance test, runs inside 86Box)
  linux/serdfsd/tests/test_frame.py (pytest)

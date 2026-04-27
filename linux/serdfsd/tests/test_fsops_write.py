"""
test_fsops_write.py — Unit tests for Phase 6 write fsops handlers.
Tests call fsops.dispatch() directly; no serial transport needed.
"""
import os
import sys
import struct
import pytest
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
from linux.serdfsd import frame as F
from linux.serdfsd import fsops
from linux.serdfsd.namemap import NameMap
from linux.serdfsd.handles import HandleTable


@pytest.fixture
def root(tmp_path):
    (tmp_path / 'README.TXT').write_text('Hello from README.\n')
    (tmp_path / 'subdir').mkdir()
    return tmp_path


@pytest.fixture
def ctx(root):
    nm = NameMap(root, rebuild=True)
    ht = HandleTable()
    return root, nm, ht


def call(ctx, cmd, payload=b''):
    root, nm, ht = ctx
    return fsops.dispatch(cmd, 1, payload, root, nm, ht)


def _path_payload(dos_path: str) -> bytes:
    return b'X' + dos_path.encode('ascii') + b'\x00'


# ── CREATE ────────────────────────────────────────────────────────────────────

def test_create_new_file(ctx):
    status, payload = call(ctx, F.CMD_CREATE, b'\x00\\NEWFILE.TXT\x00')
    assert status == F.STATUS_OK, f'status={status}'
    assert len(payload) == 1   # hid


def test_create_file_appears_on_disk(ctx):
    root, _, _ = ctx
    call(ctx, F.CMD_CREATE, b'\x00\\NEWFILE.TXT\x00')
    assert (root / 'NEWFILE.TXT').exists()


def test_create_existing_mode0_fails(ctx):
    status, _ = call(ctx, F.CMD_CREATE, b'\x00\\README.TXT\x00')
    assert status == F.ERR_EXISTS


def test_create_existing_mode1_truncates(ctx):
    root, _, _ = ctx
    status, payload = call(ctx, F.CMD_CREATE, b'\x01\\README.TXT\x00')
    assert status == F.STATUS_OK


def test_create_bad_path(ctx):
    status, _ = call(ctx, F.CMD_CREATE, b'\x01\\..\\escape.txt\x00')
    assert status == F.ERR_BAD_PATH


def test_create_in_missing_parent(ctx):
    status, _ = call(ctx, F.CMD_CREATE, b'\x00\\nosuchdir\\file.txt\x00')
    assert status == F.ERR_NOT_FOUND


# ── WRITE + CLOSE ─────────────────────────────────────────────────────────────

def test_write_and_close_round_trip(ctx):
    root, _, _ = ctx
    status, resp = call(ctx, F.CMD_CREATE, b'\x00\\WRITE.TXT\x00')
    assert status == F.STATUS_OK
    hid = resp[0]

    data = b'Hello, world!\n'
    status, _ = call(ctx, F.CMD_WRITE, bytes([hid]) + data)
    assert status == F.STATUS_OK

    status, _ = call(ctx, F.CMD_CLOSE, bytes([hid]))
    assert status == F.STATUS_OK

    assert (root / 'WRITE.TXT').read_bytes() == data


def test_write_accumulates(ctx):
    root, _, _ = ctx
    status, resp = call(ctx, F.CMD_CREATE, b'\x00\\ACCUM.TXT\x00')
    hid = resp[0]

    call(ctx, F.CMD_WRITE, bytes([hid]) + b'abc')
    call(ctx, F.CMD_WRITE, bytes([hid]) + b'def')
    call(ctx, F.CMD_CLOSE, bytes([hid]))

    assert (root / 'ACCUM.TXT').read_bytes() == b'abcdef'


def test_write_to_read_only_handle_denied(ctx):
    root, nm, ht = ctx
    # OPEN for reading
    status, resp = call(ctx, F.CMD_OPEN, b'\x00\\README.TXT\x00')
    assert status == F.STATUS_OK
    hid = resp[0]

    status, _ = call(ctx, F.CMD_WRITE, bytes([hid]) + b'data')
    assert status == F.ERR_ACCESS

    call(ctx, F.CMD_CLOSE, bytes([hid]))


def test_write_bad_handle(ctx):
    status, _ = call(ctx, F.CMD_WRITE, bytes([15]) + b'data')
    assert status == F.ERR_BAD_HANDLE


# ── FLUSH ─────────────────────────────────────────────────────────────────────

def test_flush_write_handle(ctx):
    status, resp = call(ctx, F.CMD_CREATE, b'\x00\\FLUSH.TXT\x00')
    hid = resp[0]
    call(ctx, F.CMD_WRITE, bytes([hid]) + b'flush test')
    status, _ = call(ctx, F.CMD_FLUSH, bytes([hid]))
    assert status == F.STATUS_OK
    call(ctx, F.CMD_CLOSE, bytes([hid]))


def test_flush_bad_handle(ctx):
    status, _ = call(ctx, F.CMD_FLUSH, bytes([15]))
    assert status == F.ERR_BAD_HANDLE


# ── DELETE ────────────────────────────────────────────────────────────────────

def test_delete_file(ctx):
    root, _, _ = ctx
    status, _ = call(ctx, F.CMD_DELETE, _path_payload('\\README.TXT'))
    assert status == F.STATUS_OK
    assert not (root / 'README.TXT').exists()


def test_delete_removes_alias(ctx):
    root, nm, _ = ctx
    call(ctx, F.CMD_LIST_DIR,  b'X\\\x00')  # populate aliases
    call(ctx, F.CMD_DELETE, _path_payload('\\README.TXT'))
    # After delete, alias for README.TXT should be gone
    assert nm.real_for('README.TXT', root) is None


def test_delete_not_found(ctx):
    status, _ = call(ctx, F.CMD_DELETE, _path_payload('\\NOSUCH.TXT'))
    assert status == F.ERR_NOT_FOUND


def test_delete_directory_denied(ctx):
    status, _ = call(ctx, F.CMD_DELETE, _path_payload('\\subdir'))
    assert status == F.ERR_ACCESS


def test_delete_bad_path(ctx):
    status, _ = call(ctx, F.CMD_DELETE, _path_payload('\\..\\escape'))
    assert status == F.ERR_BAD_PATH


# ── RENAME ────────────────────────────────────────────────────────────────────

def test_rename_file(ctx):
    root, _, _ = ctx
    payload = b'\\README.TXT\x00\\RENAMED.TXT\x00'
    status, _ = call(ctx, F.CMD_RENAME, payload)
    assert status == F.STATUS_OK
    assert not (root / 'README.TXT').exists()
    assert (root / 'RENAMED.TXT').exists()


def test_rename_updates_alias(ctx):
    root, nm, _ = ctx
    nm.alias_for('README.TXT', root)          # seed alias
    call(ctx, F.CMD_RENAME, b'\\README.TXT\x00\\RENAMED.TXT\x00')
    assert nm.real_for('README.TXT', root) is None
    assert nm.real_for('RENAMED.TXT', root) is not None


def test_rename_not_found(ctx):
    status, _ = call(ctx, F.CMD_RENAME, b'\\GHOST.TXT\x00\\NEW.TXT\x00')
    assert status == F.ERR_NOT_FOUND


def test_rename_destination_exists(ctx):
    root, _, _ = ctx
    (root / 'OTHER.TXT').write_text('other')
    status, _ = call(ctx, F.CMD_RENAME, b'\\README.TXT\x00\\OTHER.TXT\x00')
    assert status == F.ERR_EXISTS


def test_rename_bad_path(ctx):
    status, _ = call(ctx, F.CMD_RENAME, b'\\README.TXT\x00\\..\\escape\x00')
    assert status == F.ERR_BAD_PATH


# ── MKDIR / RMDIR ─────────────────────────────────────────────────────────────

def test_mkdir(ctx):
    root, _, _ = ctx
    status, _ = call(ctx, F.CMD_MKDIR, _path_payload('\\NEWDIR'))
    assert status == F.STATUS_OK
    assert (root / 'NEWDIR').is_dir()


def test_mkdir_existing(ctx):
    status, _ = call(ctx, F.CMD_MKDIR, _path_payload('\\subdir'))
    assert status == F.ERR_EXISTS


def test_mkdir_bad_path(ctx):
    status, _ = call(ctx, F.CMD_MKDIR, _path_payload('\\..\\escape'))
    assert status == F.ERR_BAD_PATH


def test_rmdir_empty(ctx):
    root, _, _ = ctx
    status, _ = call(ctx, F.CMD_RMDIR, _path_payload('\\subdir'))
    assert status == F.STATUS_OK
    assert not (root / 'subdir').exists()


def test_rmdir_not_found(ctx):
    status, _ = call(ctx, F.CMD_RMDIR, _path_payload('\\nosuchdir'))
    assert status == F.ERR_NOT_FOUND


def test_rmdir_non_empty(ctx):
    root, _, _ = ctx
    (root / 'subdir' / 'file.txt').write_text('content')
    status, _ = call(ctx, F.CMD_RMDIR, _path_payload('\\subdir'))
    assert status == F.ERR_ACCESS


def test_rmdir_bad_path(ctx):
    status, _ = call(ctx, F.CMD_RMDIR, _path_payload('\\..\\escape'))
    assert status == F.ERR_BAD_PATH


# ── GET_ATTR / SET_ATTR ───────────────────────────────────────────────────────

def test_get_attr_regular_file(ctx):
    status, payload = call(ctx, F.CMD_GET_ATTR, _path_payload('\\README.TXT'))
    assert status == F.STATUS_OK
    assert len(payload) == 9   # attr(1)+size(4)+date(2)+time(2)
    attr, size, date, time_ = struct.unpack_from('<BLHH', payload)
    assert attr & 0x20  # archive bit
    assert size > 0


def test_get_attr_directory(ctx):
    status, payload = call(ctx, F.CMD_GET_ATTR, _path_payload('\\subdir'))
    assert status == F.STATUS_OK
    assert len(payload) == 9
    attr = payload[0]
    assert attr & 0x10  # directory bit


def test_get_attr_not_found(ctx):
    status, _ = call(ctx, F.CMD_GET_ATTR, _path_payload('\\NOSUCH.TXT'))
    assert status == F.ERR_NOT_FOUND


def test_seek_from_start(ctx):
    root, nm, ht = ctx
    status, resp = call(ctx, F.CMD_OPEN, b'\x00\\README.TXT\x00')
    assert status == F.STATUS_OK
    hid = resp[0]

    status, resp = call(ctx, F.CMD_SEEK, bytes([hid, 0]) + struct.pack('<l', 5))
    assert status == F.STATUS_OK
    new_pos, = struct.unpack_from('<L', resp)
    assert new_pos == 5

    call(ctx, F.CMD_CLOSE, bytes([hid]))


def test_set_attr_roundtrip(ctx):
    root, _, _ = ctx
    call(ctx, F.CMD_SET_ATTR, _path_payload('\\README.TXT') + b'\x01')
    assert not os.access(str(root / 'README.TXT'), os.W_OK)
    call(ctx, F.CMD_SET_ATTR, _path_payload('\\README.TXT') + b'\x20')
    assert os.access(str(root / 'README.TXT'), os.W_OK)


# ── GET_TIME / SET_TIME ───────────────────────────────────────────────────────

def test_get_time_returns_4_bytes(ctx):
    status, payload = call(ctx, F.CMD_GET_TIME, _path_payload('\\README.TXT'))
    assert status == F.STATUS_OK
    assert len(payload) == 4


def test_set_time_and_get_time_roundtrip(ctx):
    # DOS date: 2024-06-15 = (2024-1980)<<9 | 6<<5 | 15
    date_dos = ((2024 - 1980) << 9) | (6 << 5) | 15
    # DOS time: 10:30:00 = 10<<11 | 30<<5 | 0
    time_dos = (10 << 11) | (30 << 5) | 0
    set_payload = _path_payload('\\README.TXT') + struct.pack('<HH', date_dos, time_dos)
    status, _ = call(ctx, F.CMD_SET_TIME, set_payload)
    assert status == F.STATUS_OK

    status, payload = call(ctx, F.CMD_GET_TIME, _path_payload('\\README.TXT'))
    assert status == F.STATUS_OK
    got_date, got_time = struct.unpack('<HH', payload)
    # Verify at least year/month/day match
    assert (got_date >> 9) + 1980 == 2024
    assert (got_date >> 5) & 0x0F == 6
    assert got_date & 0x1F == 15


def test_get_time_not_found(ctx):
    status, _ = call(ctx, F.CMD_GET_TIME, _path_payload('\\NOSUCH.TXT'))
    assert status == F.ERR_NOT_FOUND


def test_close_propagates_timestamp(ctx):
    """CMD_CLOSE with 5-byte payload applies DOS date/time via os.utime."""
    import datetime
    root, _, _ = ctx
    path = root / 'NEWTS.TXT'

    # Create (mode=1 = truncate/create) so handle is write-mode
    status, data = call(ctx, F.CMD_CREATE, b'\x01\\NEWTS.TXT\x00')
    assert status == F.STATUS_OK
    hid = data[0]

    # DOS date: 2023-03-17  time: 14:22:04
    date_dos = ((2023 - 1980) << 9) | (3 << 5) | 17
    time_dos = (14 << 11) | (22 << 5) | (4 // 2)
    payload = bytes([hid]) + struct.pack('<HH', date_dos, time_dos)
    status, _ = call(ctx, F.CMD_CLOSE, payload)
    assert status == F.STATUS_OK

    # Verify mtime on the file
    st = os.stat(str(path))
    dt = datetime.datetime.fromtimestamp(st.st_mtime)
    assert dt.year == 2023
    assert dt.month == 3
    assert dt.day == 17
    assert dt.hour == 14
    assert dt.minute == 22

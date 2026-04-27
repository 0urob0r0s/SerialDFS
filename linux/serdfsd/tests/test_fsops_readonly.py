"""
test_fsops_readonly.py — Unit tests for Phase 4 read-only fsops handlers.
Tests call fsops.dispatch() directly; no serial transport needed.
"""
import os
import sys
import struct
import tempfile
import pytest
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
from linux.serdfsd import frame as F
from linux.serdfsd import fsops
from linux.serdfsd.namemap import NameMap
from linux.serdfsd.handles import HandleTable


FIXTURES = Path(__file__).parent / 'fixtures'


@pytest.fixture
def root(tmp_path):
    """Temp root populated with README.TXT and a long-name file."""
    (tmp_path / 'README.TXT').write_text('Hello from README.\n')
    (tmp_path / 'VeryLongFileName.txt').write_text('long name content\n')
    (tmp_path / 'subdir').mkdir()
    (tmp_path / 'subdir' / 'NOTES.TXT').write_text('notes\n')
    return tmp_path


@pytest.fixture
def ctx(root):
    """Returns (root, namemap, handles) ready for fsops.dispatch()."""
    nm = NameMap(root, rebuild=True)
    ht = HandleTable()
    return root, nm, ht


def call(ctx, cmd, payload=b''):
    root, nm, ht = ctx
    return fsops.dispatch(cmd, 1, payload, root, nm, ht)


# ── PING ──────────────────────────────────────────────────────────────────────

def test_ping(ctx):
    status, payload = call(ctx, F.CMD_PING)
    assert status == F.STATUS_OK
    assert payload == b''


# ── GET_INFO ──────────────────────────────────────────────────────────────────

def test_get_info(ctx):
    status, payload = call(ctx, F.CMD_GET_INFO)
    assert status == F.STATUS_OK
    assert len(payload) == 4
    max_payload, ver, _ = struct.unpack('<HBB', payload)
    assert max_payload == 512
    assert ver == 1


# ── LIST_DIR_BEGIN ────────────────────────────────────────────────────────────

def _list_payload(path: str) -> bytes:
    return b'X' + path.encode('ascii') + b'\x00'


def test_list_dir_root(ctx):
    status, payload = call(ctx, F.CMD_LIST_DIR, _list_payload('\\'))
    assert status == F.STATUS_OK
    assert len(payload) >= 1       # at least the hid byte
    entries_data = payload[1:]     # skip hid prefix
    assert len(entries_data) % fsops.DIR_ENTRY_SIZE == 0
    count = len(entries_data) // fsops.DIR_ENTRY_SIZE
    assert count >= 1  # at minimum README.TXT


def test_list_dir_entry_content(ctx):
    status, payload = call(ctx, F.CMD_LIST_DIR, _list_payload('\\'))
    assert status == F.STATUS_OK
    entries_data = payload[1:]  # skip hid prefix
    names = set()
    for i in range(len(entries_data) // fsops.DIR_ENTRY_SIZE):
        entry = entries_data[i * fsops.DIR_ENTRY_SIZE:(i + 1) * fsops.DIR_ENTRY_SIZE]
        name = entry[:12].rstrip(b'\x00').decode('ascii')
        names.add(name.upper())
    assert 'README.TXT' in names


def test_list_dir_longname_aliased(ctx):
    """VeryLongFileName.txt must appear with a short 8.3 alias."""
    status, payload = call(ctx, F.CMD_LIST_DIR, _list_payload('\\'))
    assert status == F.STATUS_OK
    entries_data = payload[1:]  # skip hid prefix
    names = set()
    for i in range(len(entries_data) // fsops.DIR_ENTRY_SIZE):
        entry = entries_data[i * fsops.DIR_ENTRY_SIZE:(i + 1) * fsops.DIR_ENTRY_SIZE]
        name = entry[:12].rstrip(b'\x00').decode('ascii')
        names.add(name.upper())
    # VeryLongFileName.txt → VERYLONG.TXT or VERYLO~1.TXT etc.
    assert any('VERYLONG' in n or 'VERYLO~' in n for n in names), f'names={names}'


def test_list_dir_attr_directory(ctx):
    """subdir must appear with attr bit 0x10 (directory)."""
    status, payload = call(ctx, F.CMD_LIST_DIR, _list_payload('\\'))
    assert status == F.STATUS_OK
    entries_data = payload[1:]  # skip hid prefix
    for i in range(len(entries_data) // fsops.DIR_ENTRY_SIZE):
        entry = entries_data[i * fsops.DIR_ENTRY_SIZE:(i + 1) * fsops.DIR_ENTRY_SIZE]
        name = entry[:12].rstrip(b'\x00').decode('ascii').upper()
        if name == 'SUBDIR':
            attr = entry[12]
            assert attr & 0x10, f'subdir attr={attr:#04x}'
            break


def test_list_dir_notfound(ctx):
    status, _ = call(ctx, F.CMD_LIST_DIR, _list_payload('\\NOSUCHDIR'))
    assert status == F.ERR_NOT_FOUND


def test_list_dir_bad_path(ctx):
    """V4: traversal path must return ERR_BAD_PATH."""
    status, _ = call(ctx, F.CMD_LIST_DIR, _list_payload('\\..'))
    assert status == F.ERR_BAD_PATH


# ── OPEN / READ / CLOSE ───────────────────────────────────────────────────────

def _open_payload(path: str, mode: int = 0) -> bytes:
    return bytes([mode]) + path.encode('ascii') + b'\x00'


def test_open_read_close(ctx):
    _, nm, ht = ctx
    root, _, _ = ctx

    # OPEN
    status, resp = call(ctx, F.CMD_OPEN, _open_payload('\\README.TXT'))
    assert status == F.STATUS_OK, f'OPEN failed status={status}'
    assert len(resp) == 1
    hid = resp[0]

    # READ
    read_payload = struct.pack('<BH', hid, 512)
    status, data = call(ctx, F.CMD_READ, read_payload)
    assert status == F.STATUS_OK
    assert b'Hello from README' in data

    # CLOSE
    status, _ = call(ctx, F.CMD_CLOSE, bytes([hid]))
    assert status == F.STATUS_OK


def test_open_nonexistent(ctx):
    status, _ = call(ctx, F.CMD_OPEN, _open_payload('\\NOSUCHFILE.TXT'))
    assert status == F.ERR_NOT_FOUND


def test_open_bad_path(ctx):
    """V4: OPEN with traversal must return ERR_BAD_PATH."""
    status, _ = call(ctx, F.CMD_OPEN, _open_payload('\\..\\SECRET.TXT'))
    assert status == F.ERR_BAD_PATH


def test_open_write_unsupported(ctx):
    status, _ = call(ctx, F.CMD_OPEN, _open_payload('\\README.TXT', mode=1))
    assert status == F.ERR_UNSUPPORTED


def test_read_bad_handle(ctx):
    payload = struct.pack('<BH', 15, 512)
    status, _ = call(ctx, F.CMD_READ, payload)
    assert status == F.ERR_BAD_HANDLE


def test_close_bad_handle(ctx):
    status, _ = call(ctx, F.CMD_CLOSE, bytes([15]))
    assert status == F.ERR_BAD_HANDLE


# ── Namemap persistence ───────────────────────────────────────────────────────

def test_namemap_persists_after_scan(ctx):
    root, nm, ht = ctx
    call(ctx, F.CMD_LIST_DIR, _list_payload('\\'))
    nm.save_if_dirty()
    import json
    map_path = root / '.serialdfs-namemap.json'
    assert map_path.exists()
    with open(map_path) as f:
        data = json.load(f)
    assert data['version'] == 1
    assert 'entries' in data


def test_corrupt_namemap_rejected(tmp_path):
    """Daemon refuses to start when namemap is corrupt (fail-closed)."""
    from linux.serdfsd.namemap import NameMapError
    (tmp_path / '.serialdfs-namemap.json').write_text('{"version": 1, "entries": "bad"}')
    with pytest.raises(NameMapError):
        NameMap(tmp_path, rebuild=False)


def test_corrupt_namemap_rebuild_allowed(tmp_path):
    """--rebuild-namemap bypasses corrupt namemap check."""
    (tmp_path / '.serialdfs-namemap.json').write_text('not json at all')
    nm = NameMap(tmp_path, rebuild=True)
    assert nm is not None

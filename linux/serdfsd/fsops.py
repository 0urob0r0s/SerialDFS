"""
fsops.py — Command handlers for the SerialDFS daemon (Phase 4: read-only).

Each handler receives the decoded request payload (bytes) and returns
(status_byte, response_payload_bytes).

Directory entry binary layout (23 bytes per entry, spec §5.5):
  offset  size  field
    0      12   name_83  (NUL-padded to 12 bytes)
   12       1   attr     (DOS attribute bits)
   13       2   date_dos (LE, packed DOS date)
   15       2   time_dos (LE, packed DOS time)
   17       4   size     (LE, uint32)
   21       1   more     (1 = more entries remain, 0 = last in batch)
   22       1   reserved (0)

Total: 23 bytes.  Max 22 entries per 512-byte payload.
"""
from __future__ import annotations
import errno
import os
import stat
import struct
import logging
import datetime
from pathlib import Path

from . import frame as F
from .pathjail import ErrBadPath, resolve
from .namemap import NameMap
from .handles import HandleTable, NoFreeHandles, BadHandle, OpenHandle, DirState

log = logging.getLogger(__name__)

DIR_ENTRY_SIZE = 23
MAX_ENTRIES_PER_FRAME = 512 // DIR_ENTRY_SIZE   # = 22
SERVER_VERSION = 1

# Open mode constants
MODE_READ_ONLY  = 0
MODE_READ_WRITE = 1
MODE_WRITE_ONLY = 2


# ── Helpers ──────────────────────────────────────────────────────────────────

def _dos_date(mtime: float) -> int:
    dt = datetime.datetime.fromtimestamp(mtime)
    return ((max(dt.year - 1980, 0)) << 9) | (dt.month << 5) | dt.day


def _dos_time(mtime: float) -> int:
    dt = datetime.datetime.fromtimestamp(mtime)
    return (dt.hour << 11) | (dt.minute << 5) | (dt.second // 2)


def _encode_entry(alias: str, is_dir: bool, st, more: bool) -> bytes:
    name_bytes = alias.encode('ascii', errors='replace')[:12].ljust(12, b'\x00')
    attr       = 0x10 if is_dir else 0x20
    date       = _dos_date(st.st_mtime)
    time_      = _dos_time(st.st_mtime)
    size       = 0 if is_dir else st.st_size
    return struct.pack('<12sBHHIBB',
                       name_bytes, attr, date, time_, size,
                       1 if more else 0, 0)


# ── Command dispatch table ────────────────────────────────────────────────────

def dispatch(cmd: int, seq: int, payload: bytes,
             root: Path, namemap: NameMap, handles: HandleTable,
             ) -> tuple[int, bytes]:
    """
    Route cmd to the appropriate handler.
    Returns (status, response_payload).
    """
    handlers = {
        F.CMD_PING:          _ping,
        F.CMD_GET_INFO:      _get_info,
        F.CMD_LIST_DIR:      _list_dir_begin,
        F.CMD_LIST_DIR_NEXT: _list_dir_next,
        F.CMD_OPEN:          _open,
        F.CMD_CREATE:        _create,
        F.CMD_READ:          _read,
        F.CMD_WRITE:         _write,
        F.CMD_FLUSH:         _flush,
        F.CMD_CLOSE:         _close,
        F.CMD_DELETE:        _delete,
        F.CMD_RENAME:        _rename,
        F.CMD_MKDIR:         _mkdir,
        F.CMD_RMDIR:         _rmdir,
        F.CMD_SEEK:          _seek,
        F.CMD_GET_ATTR:      _get_attr,
        F.CMD_SET_ATTR:      _set_attr,
        F.CMD_GET_TIME:      _get_time,
        F.CMD_SET_TIME:      _set_time,
    }
    handler = handlers.get(cmd)
    if handler is None:
        log.warning('unsupported command 0x%02x', cmd)
        return F.ERR_UNSUPPORTED, b''
    try:
        return handler(payload, root, namemap, handles)
    except Exception as e:
        log.exception('error in cmd 0x%02x: %s', cmd, e)
        return F.ERR_IO, b''


# ── Handlers ─────────────────────────────────────────────────────────────────

def _ping(payload: bytes, root: Path, namemap: NameMap,
          handles: HandleTable) -> tuple[int, bytes]:
    return F.STATUS_OK, b''


def _get_info(payload: bytes, root: Path, namemap: NameMap,
              handles: HandleTable) -> tuple[int, bytes]:
    # uint16le max_payload, uint8 server_version, uint8 reserved
    return F.STATUS_OK, struct.pack('<HBB', 512, SERVER_VERSION, 0)


def _list_dir_begin(payload: bytes, root: Path, namemap: NameMap,
                    handles: HandleTable) -> tuple[int, bytes]:
    """
    Payload: 1 drive byte + NUL-terminated DOS path.
    Scans the directory, caches result in a DirState handle,
    and returns the first batch of entries (R12 mitigation: always re-scans).
    """
    if len(payload) < 2:
        return F.ERR_PROTOCOL, b''

    dos_path = payload[1:].split(b'\x00', 1)[0].decode('ascii', errors='replace')
    log.debug('LIST_DIR_BEGIN path=%r', dos_path)

    try:
        real_dir = resolve(root, dos_path or '\\', namemap)
    except ErrBadPath as e:
        log.warning('bad path: %s', e)
        return F.ERR_BAD_PATH, b''

    if not real_dir.is_dir():
        return F.ERR_NOT_FOUND, b''

    entries = namemap.scan_dir(real_dir)
    namemap.save_if_dirty()

    try:
        hid = handles.alloc_dir(real_dir, entries)
    except NoFreeHandles:
        return F.ERR_IO, b''

    status, data = _emit_dir_batch(handles.get_dir(hid), hid)
    if status == F.STATUS_OK:
        data = bytes([hid]) + data
    return status, data


def _list_dir_next(payload: bytes, root: Path, namemap: NameMap,
                   handles: HandleTable) -> tuple[int, bytes]:
    """Payload: 1 byte handle ID."""
    if not payload:
        return F.ERR_PROTOCOL, b''
    hid = payload[0]
    try:
        ds = handles.get_dir(hid)
    except BadHandle:
        return F.ERR_BAD_HANDLE, b''

    status, data = _emit_dir_batch(ds, hid)
    # Free the handle once all entries have been sent
    if status == F.STATUS_OK and not data:
        handles.free(hid)
    return status, data


def _emit_dir_batch(ds: DirState, hid: int) -> tuple[int, bytes]:
    """Build one payload batch from ds.entries starting at ds.cursor."""
    entries = ds.entries
    cursor  = ds.cursor
    total   = len(entries)

    if cursor >= total:
        return F.STATUS_OK, b''  # empty: no more entries

    batch_end = min(cursor + MAX_ENTRIES_PER_FRAME, total)
    buf = bytearray()
    for i in range(cursor, batch_end):
        more = (i < total - 1)  # more entries exist overall
        e = entries[i]
        buf += _encode_entry(e.alias, e.is_dir, e.st, more)

    ds.cursor = batch_end
    return F.STATUS_OK, bytes(buf)


def _open(payload: bytes, root: Path, namemap: NameMap,
          handles: HandleTable) -> tuple[int, bytes]:
    """
    Payload: uint8 mode + NUL-terminated DOS path.
    Phase 4: only read-only (mode=0) supported.
    """
    if len(payload) < 2:
        return F.ERR_PROTOCOL, b''

    mode     = payload[0]
    dos_path = payload[1:].split(b'\x00', 1)[0].decode('ascii', errors='replace')
    log.debug('OPEN mode=%d path=%r', mode, dos_path)

    if mode not in (MODE_READ_ONLY,):
        return F.ERR_UNSUPPORTED, b''

    try:
        real_path = resolve(root, dos_path, namemap)
    except ErrBadPath as e:
        log.warning('bad path: %s', e)
        return F.ERR_BAD_PATH, b''

    if not real_path.exists():
        return F.ERR_NOT_FOUND, b''
    if real_path.is_dir():
        return F.ERR_ACCESS, b''

    try:
        fd = os.open(str(real_path), os.O_RDONLY)
    except PermissionError:
        return F.ERR_ACCESS, b''
    except OSError:
        return F.ERR_IO, b''

    try:
        hid = handles.alloc_file(real_path, fd, flags=mode)
    except NoFreeHandles:
        os.close(fd)
        return F.ERR_IO, b''

    return F.STATUS_OK, bytes([hid])


def _read(payload: bytes, root: Path, namemap: NameMap,
          handles: HandleTable) -> tuple[int, bytes]:
    """Payload: uint8 handle + uint16le count [+ uint32le offset].

    The 7-byte (offset-explicit) form is idempotent: TSR retries on
    timeout get the same bytes back instead of advancing past chunks.
    The legacy 3-byte form falls back to position-tracked sequential
    reads (kept for SERTYPE.EXE and other non-resident tools that
    haven't been updated)."""
    if len(payload) < 3:
        return F.ERR_PROTOCOL, b''

    hid, count = struct.unpack_from('<BH', payload)
    count = min(count, 512)
    offset = None
    if len(payload) >= 7:
        offset = struct.unpack_from('<I', payload, 3)[0]
    log.debug('READ hid=%d count=%d offset=%s',
              hid, count, offset if offset is not None else 'seq')

    try:
        h = handles.get_file(hid)
    except BadHandle:
        return F.ERR_BAD_HANDLE, b''

    try:
        if offset is not None:
            data = os.pread(h.fd, count, offset)
        else:
            data = os.read(h.fd, count)
            h.offset += len(data)
    except OSError:
        return F.ERR_IO, b''

    return F.STATUS_OK, data  # empty data = EOF; STATUS_OK with 0 bytes


def _close(payload: bytes, root: Path, namemap: NameMap,
           handles: HandleTable) -> tuple[int, bytes]:
    """Payload: uint8 hid [+ date_dos(2 LE) + time_dos(2 LE)].
    Fsyncs write-mode files; applies DOS timestamp if 5-byte payload."""
    if not payload:
        return F.ERR_PROTOCOL, b''
    hid = payload[0]
    try:
        h = handles.get(hid)
    except BadHandle:
        return F.ERR_BAD_HANDLE, b''
    if isinstance(h, OpenHandle) and h.flags != MODE_READ_ONLY:
        try:
            os.fsync(h.fd)
        except OSError:
            pass
        if len(payload) >= 5:
            date_dos, time_dos = struct.unpack_from('<HH', payload, 1)
            yr = ((date_dos >> 9) & 0x7F) + 1980
            mo = max(1, (date_dos >> 5) & 0x0F)
            dy = max(1, date_dos & 0x1F)
            hr = (time_dos >> 11) & 0x1F
            mn = (time_dos >> 5) & 0x3F
            sc = (time_dos & 0x1F) * 2
            try:
                dt = datetime.datetime(yr, mo, dy, hr, mn, sc)
                ts = dt.timestamp()
                os.utime(str(h.path), (ts, ts))
            except (ValueError, OSError):
                pass  # non-fatal: file is closed regardless
    try:
        handles.free(hid)
    except BadHandle:
        return F.ERR_BAD_HANDLE, b''
    return F.STATUS_OK, b''


# ── Write handlers ────────────────────────────────────────────────────────────

def _create(payload: bytes, root: Path, namemap: NameMap,
            handles: HandleTable) -> tuple[int, bytes]:
    """
    Payload: uint8 mode + NUL-terminated DOS path.
    mode=0: create-new (ERR_EXISTS if file already exists).
    mode=1: truncate if exists, create if not.
    Returns STATUS_OK + uint8 handle ID.
    """
    if len(payload) < 2:
        return F.ERR_PROTOCOL, b''

    mode     = payload[0]
    dos_path = payload[1:].split(b'\x00', 1)[0].decode('ascii', errors='replace')
    log.debug('CREATE mode=%d path=%r', mode, dos_path)

    if mode not in (0, 1):
        return F.ERR_UNSUPPORTED, b''

    try:
        real_path = resolve(root, dos_path, namemap)
    except ErrBadPath as e:
        log.warning('bad path: %s', e)
        return F.ERR_BAD_PATH, b''

    if not real_path.parent.exists():
        return F.ERR_NOT_FOUND, b''

    open_flags = os.O_WRONLY | os.O_CREAT | (os.O_EXCL if mode == 0 else os.O_TRUNC)
    try:
        fd = os.open(str(real_path), open_flags, 0o666)
    except FileExistsError:
        return F.ERR_EXISTS, b''
    except PermissionError:
        return F.ERR_ACCESS, b''
    except OSError:
        return F.ERR_IO, b''

    namemap.alias_for(real_path.name, real_path.parent)
    namemap.save_if_dirty()

    try:
        hid = handles.alloc_file(real_path, fd, flags=MODE_WRITE_ONLY)
    except NoFreeHandles:
        os.close(fd)
        return F.ERR_IO, b''

    return F.STATUS_OK, bytes([hid])


def _write(payload: bytes, root: Path, namemap: NameMap,
           handles: HandleTable) -> tuple[int, bytes]:
    """Payload: uint8 handle + data bytes."""
    if len(payload) < 1:
        return F.ERR_PROTOCOL, b''

    hid  = payload[0]
    data = payload[1:]
    log.debug('WRITE hid=%d len=%d', hid, len(data))

    try:
        h = handles.get_file(hid)
    except BadHandle:
        return F.ERR_BAD_HANDLE, b''

    if h.flags == MODE_READ_ONLY:
        return F.ERR_ACCESS, b''

    try:
        written = os.write(h.fd, data)
        h.offset += written
    except OSError:
        return F.ERR_IO, b''

    return F.STATUS_OK, b''


def _flush(payload: bytes, root: Path, namemap: NameMap,
           handles: HandleTable) -> tuple[int, bytes]:
    """Payload: uint8 handle."""
    if not payload:
        return F.ERR_PROTOCOL, b''
    hid = payload[0]
    try:
        h = handles.get_file(hid)
    except BadHandle:
        return F.ERR_BAD_HANDLE, b''
    try:
        os.fsync(h.fd)
    except OSError:
        return F.ERR_IO, b''
    return F.STATUS_OK, b''


def _delete(payload: bytes, root: Path, namemap: NameMap,
            handles: HandleTable) -> tuple[int, bytes]:
    """Payload: uint8 drive + NUL-terminated DOS path."""
    if len(payload) < 2:
        return F.ERR_PROTOCOL, b''

    dos_path = payload[1:].split(b'\x00', 1)[0].decode('ascii', errors='replace')
    log.debug('DELETE path=%r', dos_path)

    try:
        real_path = resolve(root, dos_path, namemap)
    except ErrBadPath as e:
        log.warning('bad path: %s', e)
        return F.ERR_BAD_PATH, b''

    if not real_path.exists():
        return F.ERR_NOT_FOUND, b''
    if real_path.is_dir():
        return F.ERR_ACCESS, b''

    try:
        real_path.unlink()
    except PermissionError:
        return F.ERR_ACCESS, b''
    except OSError:
        return F.ERR_IO, b''

    namemap.remove_alias(real_path.name, real_path.parent)
    namemap.save_if_dirty()
    return F.STATUS_OK, b''


def _rename(payload: bytes, root: Path, namemap: NameMap,
            handles: HandleTable) -> tuple[int, bytes]:
    """Payload: old_path(NUL) + new_path(NUL). Both paths are full DOS paths."""
    parts = payload.split(b'\x00', 2)
    if len(parts) < 2:
        return F.ERR_PROTOCOL, b''

    old_dos = parts[0].decode('ascii', errors='replace')
    new_dos = parts[1].decode('ascii', errors='replace')
    log.debug('RENAME old=%r new=%r', old_dos, new_dos)

    if not old_dos or not new_dos:
        return F.ERR_PROTOCOL, b''

    try:
        old_real = resolve(root, old_dos, namemap)
        new_real = resolve(root, new_dos, namemap)
    except ErrBadPath as e:
        log.warning('bad path: %s', e)
        return F.ERR_BAD_PATH, b''

    if not old_real.exists():
        return F.ERR_NOT_FOUND, b''
    if new_real.exists():
        return F.ERR_EXISTS, b''
    if not new_real.parent.exists():
        return F.ERR_NOT_FOUND, b''

    try:
        old_real.rename(new_real)
    except PermissionError:
        return F.ERR_ACCESS, b''
    except OSError:
        return F.ERR_IO, b''

    namemap.remove_alias(old_real.name, old_real.parent)
    namemap.alias_for(new_real.name, new_real.parent)
    namemap.save_if_dirty()
    return F.STATUS_OK, b''


def _mkdir(payload: bytes, root: Path, namemap: NameMap,
           handles: HandleTable) -> tuple[int, bytes]:
    """Payload: uint8 drive + NUL-terminated DOS path."""
    if len(payload) < 2:
        return F.ERR_PROTOCOL, b''

    dos_path = payload[1:].split(b'\x00', 1)[0].decode('ascii', errors='replace')
    log.debug('MKDIR path=%r', dos_path)

    try:
        real_path = resolve(root, dos_path, namemap)
    except ErrBadPath as e:
        log.warning('bad path: %s', e)
        return F.ERR_BAD_PATH, b''

    if real_path.exists():
        return F.ERR_EXISTS, b''
    if not real_path.parent.exists():
        return F.ERR_NOT_FOUND, b''

    try:
        real_path.mkdir()
    except FileExistsError:
        return F.ERR_EXISTS, b''
    except PermissionError:
        return F.ERR_ACCESS, b''
    except OSError:
        return F.ERR_IO, b''

    namemap.alias_for(real_path.name, real_path.parent)
    namemap.save_if_dirty()
    return F.STATUS_OK, b''


def _rmdir(payload: bytes, root: Path, namemap: NameMap,
           handles: HandleTable) -> tuple[int, bytes]:
    """Payload: uint8 drive + NUL-terminated DOS path."""
    if len(payload) < 2:
        return F.ERR_PROTOCOL, b''

    dos_path = payload[1:].split(b'\x00', 1)[0].decode('ascii', errors='replace')
    log.debug('RMDIR path=%r', dos_path)

    try:
        real_path = resolve(root, dos_path, namemap)
    except ErrBadPath as e:
        log.warning('bad path: %s', e)
        return F.ERR_BAD_PATH, b''

    if not real_path.exists():
        return F.ERR_NOT_FOUND, b''
    if not real_path.is_dir():
        return F.ERR_ACCESS, b''

    try:
        real_path.rmdir()
    except OSError as e:
        if e.errno == errno.ENOTEMPTY:
            return F.ERR_ACCESS, b''
        return F.ERR_IO, b''

    namemap.remove_alias(real_path.name, real_path.parent)
    namemap.save_if_dirty()
    return F.STATUS_OK, b''


def _seek(payload: bytes, root: Path, namemap: NameMap,
          handles: HandleTable) -> tuple[int, bytes]:
    """Payload: uint8 hid + uint8 whence + int32le offset.
    whence: 0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END.
    Returns STATUS_OK + uint32le new_pos."""
    if len(payload) < 6:
        return F.ERR_PROTOCOL, b''
    hid    = payload[0]
    whence = payload[1]
    offset = struct.unpack_from('<l', payload, 2)[0]  # signed 32-bit
    if whence not in (0, 1, 2):
        return F.ERR_PROTOCOL, b''
    try:
        h = handles.get_file(hid)
    except BadHandle:
        return F.ERR_BAD_HANDLE, b''
    try:
        new_pos = os.lseek(h.fd, offset, whence)
    except OSError:
        return F.ERR_IO, b''
    h.offset = new_pos
    return F.STATUS_OK, struct.pack('<L', new_pos)


def _get_attr(payload: bytes, root: Path, namemap: NameMap,
              handles: HandleTable) -> tuple[int, bytes]:
    """Payload: uint8 drive + NUL-terminated DOS path.
    Returns STATUS_OK + attr(1) + size(4 LE) + date(2 LE) + time(2 LE) = 9 bytes."""
    if len(payload) < 2:
        return F.ERR_PROTOCOL, b''

    dos_path = payload[1:].split(b'\x00', 1)[0].decode('ascii', errors='replace')

    try:
        real_path = resolve(root, dos_path, namemap)
    except ErrBadPath as e:
        log.warning('bad path: %s', e)
        return F.ERR_BAD_PATH, b''

    if not real_path.exists():
        return F.ERR_NOT_FOUND, b''

    st   = real_path.stat()
    attr = 0x10 if real_path.is_dir() else 0x20
    if not os.access(str(real_path), os.W_OK):
        attr |= 0x01  # read-only
    size  = 0 if real_path.is_dir() else st.st_size
    date  = _dos_date(st.st_mtime)
    time_ = _dos_time(st.st_mtime)
    return F.STATUS_OK, struct.pack('<BLHH', attr, size, date, time_)


def _set_attr(payload: bytes, root: Path, namemap: NameMap,
              handles: HandleTable) -> tuple[int, bytes]:
    """Payload: uint8 drive + NUL-terminated DOS path + uint8 attr."""
    if len(payload) < 3:
        return F.ERR_PROTOCOL, b''

    parts = payload[1:].split(b'\x00', 1)
    if len(parts) < 2 or len(parts[1]) < 1:
        return F.ERR_PROTOCOL, b''

    dos_path = parts[0].decode('ascii', errors='replace')
    attr     = parts[1][0]

    try:
        real_path = resolve(root, dos_path, namemap)
    except ErrBadPath as e:
        log.warning('bad path: %s', e)
        return F.ERR_BAD_PATH, b''

    if not real_path.exists():
        return F.ERR_NOT_FOUND, b''

    try:
        cur = os.stat(str(real_path)).st_mode
        if attr & 0x01:   # read-only
            new_mode = cur & ~(stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH)
        else:
            new_mode = cur | stat.S_IWUSR
        os.chmod(str(real_path), new_mode)
    except OSError:
        return F.ERR_IO, b''

    return F.STATUS_OK, b''


def _get_time(payload: bytes, root: Path, namemap: NameMap,
              handles: HandleTable) -> tuple[int, bytes]:
    """Payload: uint8 drive + NUL-terminated DOS path.
    Returns STATUS_OK + date_dos(2 LE) + time_dos(2 LE)."""
    if len(payload) < 2:
        return F.ERR_PROTOCOL, b''

    dos_path = payload[1:].split(b'\x00', 1)[0].decode('ascii', errors='replace')

    try:
        real_path = resolve(root, dos_path, namemap)
    except ErrBadPath as e:
        log.warning('bad path: %s', e)
        return F.ERR_BAD_PATH, b''

    if not real_path.exists():
        return F.ERR_NOT_FOUND, b''

    st    = real_path.stat()
    date  = _dos_date(st.st_mtime)
    time_ = _dos_time(st.st_mtime)
    return F.STATUS_OK, struct.pack('<HH', date, time_)


def _set_time(payload: bytes, root: Path, namemap: NameMap,
              handles: HandleTable) -> tuple[int, bytes]:
    """Payload: uint8 drive + NUL-terminated DOS path + date_dos(2 LE) + time_dos(2 LE)."""
    if len(payload) < 7:
        return F.ERR_PROTOCOL, b''

    parts = payload[1:].split(b'\x00', 1)
    if len(parts) < 2 or len(parts[1]) < 4:
        return F.ERR_PROTOCOL, b''

    dos_path = parts[0].decode('ascii', errors='replace')
    date_dos, time_dos = struct.unpack_from('<HH', parts[1], 0)

    try:
        real_path = resolve(root, dos_path, namemap)
    except ErrBadPath as e:
        log.warning('bad path: %s', e)
        return F.ERR_BAD_PATH, b''

    if not real_path.exists():
        return F.ERR_NOT_FOUND, b''

    yr = ((date_dos >> 9) & 0x7F) + 1980
    mo = (date_dos >> 5) & 0x0F
    dy = date_dos & 0x1F
    hr = (time_dos >> 11) & 0x1F
    mn = (time_dos >> 5) & 0x3F
    sc = (time_dos & 0x1F) * 2

    try:
        dt = datetime.datetime(yr, mo, dy, hr, mn, sc)
        ts = dt.timestamp()
        os.utime(str(real_path), (ts, ts))
    except (ValueError, OSError):
        return F.ERR_IO, b''

    return F.STATUS_OK, b''

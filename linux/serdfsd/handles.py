"""
handles.py — Bounded open-handle pool (max 16 handles, IDs 0-15).

OpenHandle: a file opened for reading or writing.
DirState:   a directory enumeration in progress.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from pathlib import Path
from typing import Union

MAX_HANDLES = 16


class NoFreeHandles(Exception):
    pass


class BadHandle(Exception):
    pass


@dataclass
class OpenHandle:
    path:   Path
    fd:     int       # real OS file descriptor
    offset: int = 0
    flags:  int = 0   # 0=read-only, 1=read-write, 2=write-only


@dataclass
class DirState:
    path:    Path
    entries: list     # list of namemap.DirEntry
    cursor:  int = 0  # next entry index to return


Handle = Union[OpenHandle, DirState]


class HandleTable:
    def __init__(self) -> None:
        self._slots: list[Handle | None] = [None] * MAX_HANDLES

    def _alloc(self, handle: Handle) -> int:
        for i, slot in enumerate(self._slots):
            if slot is None:
                self._slots[i] = handle
                return i
        raise NoFreeHandles("all handles in use")

    def alloc_file(self, path: Path, fd: int, flags: int = 0) -> int:
        return self._alloc(OpenHandle(path=path, fd=fd, flags=flags))

    def alloc_dir(self, path: Path, entries: list) -> int:
        return self._alloc(DirState(path=path, entries=entries))

    def get(self, hid: int) -> Handle:
        if hid < 0 or hid >= MAX_HANDLES or self._slots[hid] is None:
            raise BadHandle(f"invalid handle {hid}")
        return self._slots[hid]

    def get_file(self, hid: int) -> OpenHandle:
        h = self.get(hid)
        if not isinstance(h, OpenHandle):
            raise BadHandle(f"handle {hid} is not a file handle")
        return h

    def get_dir(self, hid: int) -> DirState:
        h = self.get(hid)
        if not isinstance(h, DirState):
            raise BadHandle(f"handle {hid} is not a directory handle")
        return h

    def free(self, hid: int) -> None:
        if hid < 0 or hid >= MAX_HANDLES or self._slots[hid] is None:
            raise BadHandle(f"invalid handle {hid}")
        h = self._slots[hid]
        if isinstance(h, OpenHandle):
            try:
                import os
                os.close(h.fd)
            except OSError:
                pass
        self._slots[hid] = None

"""
pathjail.py — Root-jail path resolution (R8 mitigation).

Strategy (order is non-negotiable):
  1. Strip drive prefix; replace backslash with forward slash.
  2. Reject any component equal to '..' or containing NUL/control chars.
  3. Walk components, resolving 8.3 aliases via namemap (if provided).
  4. Build candidate path under root.
  5. os.path.realpath(candidate) → assert canonical starts with root.

Symlinks that escape root are treated as missing (ERR_BAD_PATH).
"""
from __future__ import annotations
import os
from pathlib import Path


class ErrBadPath(Exception):
    pass


def resolve(root: Path, dos_path: str, namemap=None) -> Path:
    """
    Resolve a DOS 8.3 path relative to root, enforcing the root jail.

    dos_path may include a drive prefix ("X:\\...") or be bare ("\\...").
    namemap is a NameMap instance for alias→real-name lookup (may be None).

    Returns the resolved absolute Path on success.
    Raises ErrBadPath on traversal attempt, invalid chars, or symlink escape.
    """
    path = dos_path

    # Strip optional drive prefix (e.g. "X:" or "x:")
    if len(path) >= 2 and path[1] == ':':
        path = path[2:]

    # Normalize separators; do NOT uppercase yet — namemap lookups are case-aware
    path = path.replace('\\', '/')

    # Split and filter empty parts (handles leading/trailing slashes)
    components = [c for c in path.split('/') if c]

    # Security gate: reject '..' and control characters in any component
    for c in components:
        if c == '..':
            raise ErrBadPath(f"path traversal: component '..' in '{dos_path}'")
        if '\x00' in c or any(ord(ch) < 0x20 for ch in c):
            raise ErrBadPath(f"invalid character in path component '{c}'")

    # Resolve root to absolute real path once
    root_abs = Path(os.path.realpath(root))

    # Walk components, mapping 8.3 aliases to real on-disk names
    current = root_abs
    for comp in components:
        comp_upper = comp.upper()

        # 1) namemap lookup (alias → real name)
        real_name: str | None = None
        if namemap is not None:
            real_name = namemap.real_for(comp_upper, current)

        # 2) case-insensitive directory scan fallback
        if real_name is None:
            real_name = _find_icase(current, comp_upper)

        # 3) no match — keep the component as given (may not exist yet; that's OK)
        if real_name is None:
            real_name = comp_upper

        current = current / real_name

    # Final jail check via realpath (resolves any remaining symlinks)
    try:
        canonical = Path(os.path.realpath(current))
    except OSError:
        raise ErrBadPath(f"cannot resolve '{dos_path}'")

    if canonical != root_abs and not str(canonical).startswith(str(root_abs) + os.sep):
        raise ErrBadPath(
            f"path escapes root: '{canonical}' is not under '{root_abs}'"
        )

    return canonical


def _find_icase(directory: Path, name_upper: str) -> str | None:
    """
    Case-insensitive filename search within directory.
    Returns the first entry whose name.upper() == name_upper, or None.
    """
    try:
        for entry in directory.iterdir():
            if entry.name.upper() == name_upper:
                return entry.name
    except (OSError, NotADirectoryError):
        pass
    return None

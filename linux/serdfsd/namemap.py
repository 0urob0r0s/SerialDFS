"""
namemap.py — Persistent 8.3 alias map (R7: sticky aliases, R12: re-scan).

Aliases are sticky: once committed to JSON they never change, even if the
generation algorithm changes.  Atomic write via tmp-file + os.replace.
On corrupt JSON the daemon refuses to serve unless --rebuild-namemap is given.
"""
from __future__ import annotations
import os
import json
import stat
from pathlib import Path
from typing import NamedTuple

NAMEMAP_FILE = '.serialdfs-namemap.json'
_DOS_LEGAL = frozenset(
    'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_!@#$%^&()-{}\'`~'
)


class DirEntry(NamedTuple):
    alias:    str    # 8.3 uppercase alias
    name:     str    # real on-disk name
    st:       object # os.stat_result
    is_dir:   bool


class NameMapError(Exception):
    pass


class NameMap:
    """
    Manages an 8.3 alias↔real-name mapping for one backend root.

    JSON schema:
      {
        "version": 1,
        "mode": "compat",
        "entries": {
          "relative/dir/path": {
            "ALIAS8.TXT": "real_long_name.txt"
          }
        }
      }
    """

    def __init__(self, root: Path, mode: str = 'compat', rebuild: bool = False):
        self.root = Path(os.path.realpath(root))
        self.mode = mode
        self._path = self.root / NAMEMAP_FILE
        # {dir_key: {alias_upper: real_name}}
        self._data: dict[str, dict[str, str]] = {}
        self._dirty = False
        self._load(rebuild)

    # ── I/O ──────────────────────────────────────────────────────────────────

    def _load(self, rebuild: bool) -> None:
        if not self._path.exists():
            return
        try:
            with open(self._path, 'r', encoding='utf-8') as f:
                raw = json.load(f)
            if not isinstance(raw, dict) or raw.get('version') != 1:
                raise NameMapError('schema version mismatch')
            entries = raw.get('entries', {})
            if not isinstance(entries, dict):
                raise NameMapError("'entries' is not a dict")
            self._data = entries
        except (json.JSONDecodeError, KeyError, TypeError, NameMapError) as e:
            if not rebuild:
                raise NameMapError(
                    f"corrupt namemap {self._path}: {e}. "
                    f"Pass --rebuild-namemap to regenerate."
                ) from e
            self._data = {}

    def save(self) -> None:
        """Atomically persist the alias table."""
        tmp = str(self._path) + '.tmp'
        payload = {
            'version': 1,
            'mode':    self.mode,
            'entries': self._data,
        }
        with open(tmp, 'w', encoding='utf-8') as f:
            json.dump(payload, f, indent=2, ensure_ascii=False)
        os.replace(tmp, str(self._path))
        self._dirty = False

    def save_if_dirty(self) -> None:
        if self._dirty:
            self.save()

    # ── Public API ────────────────────────────────────────────────────────────

    def real_for(self, alias_upper: str, directory: Path) -> str | None:
        """Return the real on-disk name for a given 8.3 alias, or None."""
        key = self._dir_key(directory)
        return self._data.get(key, {}).get(alias_upper.upper())

    def alias_for(self, real_name: str, directory: Path) -> str:
        """
        Return (and if necessary create) the sticky 8.3 alias for real_name.
        New aliases are registered immediately; call save_if_dirty() afterwards.
        """
        key = self._dir_key(directory)
        if key not in self._data:
            self._data[key] = {}
        bucket = self._data[key]

        # Already mapped?
        for alias, rn in bucket.items():
            if rn.lower() == real_name.lower():
                return alias

        # Generate candidate alias
        candidate = _name_to_83(real_name)
        n = 1
        while candidate in bucket and bucket[candidate].lower() != real_name.lower():
            candidate = _tilde_suffix(_name_to_83(real_name), n)
            n += 1
            if n > 9999:
                raise NameMapError(f"too many alias collisions for '{real_name}'")

        bucket[candidate] = real_name
        self._dirty = True
        return candidate

    def remove_alias(self, real_name: str, directory: Path) -> None:
        """Remove the alias mapping for real_name from directory's bucket."""
        key = self._dir_key(directory)
        bucket = self._data.get(key, {})
        alias = None
        for a, rn in list(bucket.items()):
            if rn.lower() == real_name.lower():
                alias = a
                break
        if alias:
            del bucket[alias]
            if not bucket:
                del self._data[key]
            self._dirty = True

    def scan_dir(self, directory: Path) -> list[DirEntry]:
        """
        Scan directory, generate/reconcile aliases, return sorted DirEntry list.
        Skips the namemap file itself and entries that raise OSError.
        R12 mitigation: always re-scans on each LIST_DIR_BEGIN call.
        """
        results: list[DirEntry] = []
        try:
            raw = sorted(directory.iterdir(), key=lambda e: e.name.upper())
        except (OSError, PermissionError):
            return results

        for entry in raw:
            if entry.name == NAMEMAP_FILE:
                continue
            try:
                st = entry.stat()
            except OSError:
                continue
            alias   = self.alias_for(entry.name, directory)
            is_dir  = entry.is_dir()
            results.append(DirEntry(alias, entry.name, st, is_dir))

        return results

    # ── Helpers ───────────────────────────────────────────────────────────────

    def _dir_key(self, directory: Path) -> str:
        try:
            rel = directory.relative_to(self.root)
            return str(rel)
        except ValueError:
            return str(directory)


# ── Alias generation ─────────────────────────────────────────────────────────

def _name_to_83(name: str) -> str:
    """Generate a base 8.3 alias from a filename (no collision suffix)."""
    n = name.upper()
    # Split at last dot, but not for dotfiles like ".gitignore"
    dot = n.rfind('.')
    if dot > 0:
        base = n[:dot]
        ext  = n[dot + 1:]
    else:
        base = n
        ext  = ''

    base = _keep_legal(base)[:8]
    ext  = _keep_legal(ext)[:3]
    if not base:
        base = '_'

    return f'{base}.{ext}' if ext else base


def _keep_legal(s: str) -> str:
    return ''.join(c for c in s if c in _DOS_LEGAL)


def _tilde_suffix(alias: str, n: int) -> str:
    """Apply a ~N collision suffix, respecting 8.3 constraints."""
    suffix = f'~{n}'
    dot = alias.rfind('.')
    if dot > 0:
        base = alias[:dot]
        ext  = alias[dot:]          # includes the dot
    else:
        base = alias
        ext  = ''
    max_base = 8 - len(suffix)
    return (base[:max_base] + suffix + ext)

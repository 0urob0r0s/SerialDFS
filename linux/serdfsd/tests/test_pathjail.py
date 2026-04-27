"""
test_pathjail.py — pathjail.resolve() conformance tests.
V4 is the critical gate: path traversal MUST return ErrBadPath.
"""
import os
import sys
import tempfile
import pytest
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
from linux.serdfsd.pathjail import resolve, ErrBadPath


@pytest.fixture
def tmp_root(tmp_path):
    """A temporary root with a known structure."""
    (tmp_path / 'README.TXT').write_text('hello')
    (tmp_path / 'subdir').mkdir()
    (tmp_path / 'subdir' / 'NOTES.TXT').write_text('notes')
    # A file outside root that a traversal might target
    secret = tmp_path.parent / 'SECRET.TXT'
    secret.write_text('secret')
    yield tmp_path
    # cleanup
    if secret.exists():
        secret.unlink()


# ── Normal resolution ─────────────────────────────────────────────────────────

def test_root_resolves(tmp_root):
    p = resolve(tmp_root, '\\')
    assert p == Path(os.path.realpath(tmp_root))


def test_file_in_root(tmp_root):
    p = resolve(tmp_root, '\\README.TXT')
    assert p.name.upper() == 'README.TXT'
    assert p.exists()


def test_file_in_subdir(tmp_root):
    p = resolve(tmp_root, '\\subdir\\NOTES.TXT')
    assert p.exists()


def test_drive_prefix_stripped(tmp_root):
    p = resolve(tmp_root, 'X:\\README.TXT')
    assert p.name.upper() == 'README.TXT'


def test_lowercase_drive_stripped(tmp_root):
    p = resolve(tmp_root, 'x:\\README.TXT')
    assert p.name.upper() == 'README.TXT'


def test_nonexistent_path_ok(tmp_root):
    """Non-existent paths are allowed — caller checks existence."""
    p = resolve(tmp_root, '\\NEWFILE.TXT')
    assert str(p).startswith(str(os.path.realpath(tmp_root)))


# ── V4: traversal rejection ───────────────────────────────────────────────────

def test_v4_dotdot_at_root(tmp_root):
    """V4: X:\\..\SECRET.TXT must raise ErrBadPath (critical R8 gate)."""
    with pytest.raises(ErrBadPath):
        resolve(tmp_root, 'X:\\..\\SECRET.TXT')


def test_v4_dotdot_in_middle(tmp_root):
    with pytest.raises(ErrBadPath):
        resolve(tmp_root, '\\subdir\\..\\..\\SECRET.TXT')


def test_v4_bare_dotdot(tmp_root):
    with pytest.raises(ErrBadPath):
        resolve(tmp_root, '..')


def test_nul_in_component(tmp_root):
    with pytest.raises(ErrBadPath):
        resolve(tmp_root, '\\bad\x00name.txt')


def test_control_char_in_component(tmp_root):
    with pytest.raises(ErrBadPath):
        resolve(tmp_root, '\\bad\x01name.txt')


# ── Symlink escape ────────────────────────────────────────────────────────────

def test_symlink_escape_rejected(tmp_root):
    """A symlink inside root pointing outside root must be rejected."""
    link = tmp_root / 'ESCAPE.LNK'
    target = tmp_root.parent  # outside root
    try:
        link.symlink_to(target)
        with pytest.raises(ErrBadPath):
            resolve(tmp_root, '\\ESCAPE.LNK')
    finally:
        if link.exists() or link.is_symlink():
            link.unlink()


def test_symlink_within_root_allowed(tmp_root):
    """A symlink inside root pointing to another file inside root is OK."""
    link = tmp_root / 'LINK.TXT'
    try:
        link.symlink_to(tmp_root / 'README.TXT')
        p = resolve(tmp_root, '\\LINK.TXT')
        assert str(p).startswith(str(os.path.realpath(tmp_root)))
    finally:
        if link.exists() or link.is_symlink():
            link.unlink()

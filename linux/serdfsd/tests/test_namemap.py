"""
test_namemap.py — NameMap V1/V2/V3 conformance + R7 persistence gate.

V1: A short 8.3-legal name keeps its exact uppercase form.
V2: A name longer than 8.3 gets a truncated alias.
V3: Two names that would produce the same truncated alias are disambiguated
    with a ~N suffix.
R7 gate: aliases survive a daemon restart (JSON round-trip).
"""
import os
import sys
import json
import pytest
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
from linux.serdfsd.namemap import NameMap, NameMapError, NAMEMAP_FILE


# ── V1: short 8.3-legal name ─────────────────────────────────────────────────

def test_v1_short_name_unchanged(tmp_path):
    """V1: README.TXT (8.3-legal) → alias == 'README.TXT'."""
    nm = NameMap(tmp_path, rebuild=True)
    alias = nm.alias_for('README.TXT', tmp_path)
    assert alias == 'README.TXT'


def test_v1_uppercase_conversion(tmp_path):
    """V1: lowercase short name is uppercased in alias."""
    nm = NameMap(tmp_path, rebuild=True)
    alias = nm.alias_for('notes.txt', tmp_path)
    assert alias == 'NOTES.TXT'


def test_v1_no_extension(tmp_path):
    """V1: Name with no extension (≤8 chars) stays as-is (uppercase)."""
    nm = NameMap(tmp_path, rebuild=True)
    alias = nm.alias_for('MAKEFILE', tmp_path)
    assert alias == 'MAKEFILE'


# ── V2: long-name truncation ──────────────────────────────────────────────────

def test_v2_long_base_truncated(tmp_path):
    """V2: Base name > 8 chars is truncated to 8."""
    nm = NameMap(tmp_path, rebuild=True)
    alias = nm.alias_for('VeryLongFileName.txt', tmp_path)
    base = alias.split('.')[0]
    assert len(base) <= 8


def test_v2_long_ext_truncated(tmp_path):
    """V2: Extension > 3 chars is truncated to 3."""
    nm = NameMap(tmp_path, rebuild=True)
    alias = nm.alias_for('file.longext', tmp_path)
    parts = alias.split('.')
    assert len(parts[-1]) <= 3


def test_v2_alias_is_valid_83(tmp_path):
    """V2: Generated alias has valid 8.3 structure."""
    nm = NameMap(tmp_path, rebuild=True)
    alias = nm.alias_for('VeryLongFileName.txt', tmp_path)
    parts = alias.split('.', 1)
    assert 1 <= len(parts[0]) <= 8
    if len(parts) > 1:
        assert len(parts[1]) <= 3


# ── V3: collision disambiguation ──────────────────────────────────────────────

def test_v3_collision_gets_tilde_suffix(tmp_path):
    """V3: Two names truncating to same base get different aliases."""
    nm = NameMap(tmp_path, rebuild=True)
    a1 = nm.alias_for('VeryLongName1.txt', tmp_path)
    a2 = nm.alias_for('VeryLongName2.txt', tmp_path)
    assert a1 != a2


def test_v3_first_alias_has_no_tilde(tmp_path):
    """V3: The first collision winner keeps the clean alias."""
    nm = NameMap(tmp_path, rebuild=True)
    a1 = nm.alias_for('VeryLongName1.txt', tmp_path)
    assert '~' not in a1


def test_v3_second_alias_has_tilde(tmp_path):
    """V3: The second collision loser gets a ~N alias."""
    nm = NameMap(tmp_path, rebuild=True)
    nm.alias_for('VeryLongName1.txt', tmp_path)
    a2 = nm.alias_for('VeryLongName2.txt', tmp_path)
    assert '~' in a2


def test_v3_three_way_collision(tmp_path):
    """V3: Third name that collides gets ~2."""
    nm = NameMap(tmp_path, rebuild=True)
    nm.alias_for('VeryLongName1.txt', tmp_path)
    nm.alias_for('VeryLongName2.txt', tmp_path)
    a3 = nm.alias_for('VeryLongName3.txt', tmp_path)
    assert '~2' in a3


# ── R7: persistence across restart ───────────────────────────────────────────

def test_r7_aliases_survive_restart(tmp_path):
    """R7: Aliases are identical after saving and reloading the JSON."""
    nm1 = NameMap(tmp_path, rebuild=True)
    a1 = nm1.alias_for('README.TXT', tmp_path)
    a2 = nm1.alias_for('VeryLongFileName.txt', tmp_path)
    a3 = nm1.alias_for('VeryLongFileName2.txt', tmp_path)
    nm1.save()

    nm2 = NameMap(tmp_path, rebuild=False)
    assert nm2.alias_for('README.TXT', tmp_path)         == a1
    assert nm2.alias_for('VeryLongFileName.txt', tmp_path)  == a2
    assert nm2.alias_for('VeryLongFileName2.txt', tmp_path) == a3


def test_r7_aliases_stable_across_double_restart(tmp_path):
    """R7: Aliases do not shift on a second save+reload cycle."""
    nm1 = NameMap(tmp_path, rebuild=True)
    nm1.alias_for('VeryLongName1.txt', tmp_path)
    nm1.alias_for('VeryLongName2.txt', tmp_path)
    nm1.save()

    nm2 = NameMap(tmp_path)
    nm2.save()

    nm3 = NameMap(tmp_path)
    aliases3 = {nm3.alias_for('VeryLongName1.txt', tmp_path),
                nm3.alias_for('VeryLongName2.txt', tmp_path)}
    assert len(aliases3) == 2


def test_r7_new_alias_after_restart(tmp_path):
    """R7: New files added after a restart get fresh aliases without disturbing old ones."""
    nm1 = NameMap(tmp_path, rebuild=True)
    a1 = nm1.alias_for('README.TXT', tmp_path)
    nm1.save()

    nm2 = NameMap(tmp_path)
    a2 = nm2.alias_for('NOTES.TXT', tmp_path)
    nm2.save()

    nm3 = NameMap(tmp_path)
    assert nm3.alias_for('README.TXT', tmp_path) == a1
    assert nm3.alias_for('NOTES.TXT', tmp_path)  == a2


# ── remove_alias ─────────────────────────────────────────────────────────────

def test_remove_alias_frees_slot(tmp_path):
    """After remove_alias, the same alias can be assigned to a new file."""
    nm = NameMap(tmp_path, rebuild=True)
    a1 = nm.alias_for('VeryLongName1.txt', tmp_path)  # gets clean alias
    nm.alias_for('VeryLongName2.txt', tmp_path)        # gets ~1 alias
    nm.remove_alias('VeryLongName1.txt', tmp_path)
    # VeryLongName3 can now claim the clean alias
    a3 = nm.alias_for('VeryLongName3.txt', tmp_path)
    assert a3 == a1


def test_remove_alias_marks_dirty(tmp_path):
    nm = NameMap(tmp_path, rebuild=True)
    nm.alias_for('README.TXT', tmp_path)
    nm.save()  # clear dirty flag
    nm.remove_alias('README.TXT', tmp_path)
    assert nm._dirty is True


def test_remove_nonexistent_alias_noop(tmp_path):
    """Removing an alias that was never registered is a no-op."""
    nm = NameMap(tmp_path, rebuild=True)
    nm.remove_alias('GHOST.TXT', tmp_path)  # must not raise
    assert nm._dirty is False

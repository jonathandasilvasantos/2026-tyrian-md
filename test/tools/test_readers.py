"""Readers of the PC data (tools/tyrdata) against known facts of the
Tyrian 2.1 files (docs/data_formats.md)."""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from tyrdata import binfile, data_path, items, level, shapes  # noqa: E402

pytestmark = pytest.mark.skipif(not os.path.exists(data_path("tyrian1.lvl")), reason="no tyrian-data")


def encrypt(plain):
    """Inverse of binfile.decrypt (front to back, chaining on output)."""
    b = bytearray(plain)
    for i in range(len(b)):
        if i > 0:
            b[i] ^= b[i - 1]
        b[i] ^= binfile.CIPHER_KEY[i % 10]
    return bytes(b)


def test_cipher_roundtrip():
    msg = b"]L[ 9999 004 TYRIAN   18 09"
    assert binfile.decrypt(encrypt(msg)) == msg


@pytest.mark.parametrize("episode,levels", [(1, 18), (2, 12), (3, 12), (4, 20)])
def test_every_level_parses(episode, levels):
    assert level.level_count(episode) == levels
    for n in range(1, levels + 1):
        lvl = level.load_level(episode, n)    # asserts the 22368-byte map block
        assert os.path.exists(data_path(lvl.shape_file))
        assert lvl.events   # types above 82 occur (1.11, 4.13, 4.18): the engine ignores them
        assert len(lvl.enemies) <= 40


def test_script_level_commands_point_at_levels():
    for ep in range(1, 5):
        cmds = [level.parse_level_cmd(s) for s in level.load_script(ep) if s.startswith("]L")]
        assert cmds
        assert all(1 <= c.lvl_file_num <= level.level_count(ep) for c in cmds)


@pytest.mark.parametrize("episode", [1, 4])
def test_item_tables(episode):
    it = items.load_items(episode)       # asserts counts and the 137192-byte block
    assert it["ships"][1]["name"].startswith("USP Talon")
    assert len(it["enemies"]) == 851


def test_tiles_and_sheets():
    tiles = shapes.load_tiles("shapesz.dat")
    assert len(tiles) == shapes.TILE_RECORDS
    assert sum(t is not None for t in tiles) == 361
    sheet = shapes.main_shape_tables()[8]        # player ships
    assert shapes.sprite2_count(sheet) == 304
    spr = shapes.decode_sprite2(sheet, 1)
    assert any(p is not None for row in spr for p in row)


def test_sprite_tables_and_pics():
    font = shapes.decode_sprite_table(shapes.main_shape_tables()[0])
    assert len(font) == 60
    pic, pal = shapes.load_pic(2)
    assert len(pic) == 64000 and pal == 7


def test_superbomb_chain_artwork_dependencies():
    it = items.load_items(1)
    assert {535, 66} <= items.weapon_dependencies(it, {535})


def test_weapon_dependency_cycles_and_invalid_ids():
    it = {"weapons": [{"attack": []}, {"attack": [102]}, {"attack": [101]}]}
    assert items.weapon_dependencies(it, {0, 1, 999}) == {1, 2}

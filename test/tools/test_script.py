"""Episode bytecode keeps the original levels, destinations and item sources."""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from tyrdata import data_path, level, script  # noqa: E402

pytestmark = pytest.mark.skipif(not os.path.exists(data_path("tyrian1.lvl")), reason="no tyrian-data")


@pytest.mark.parametrize("episode,first", [(1, 8), (2, 0), (3, 2), (4, 3)])
def test_episode_levels_and_destinations(episode, first):
    ops, sections, shops, names, pages = script.compile_script(episode)
    assert script.first_level(episode) == first
    pc = 0
    seen = []
    while pc < len(ops):
        op = ops[pc]
        size = 1
        if op == script.OP_LEVEL:
            nxt, lv, song, flags, name = ops[pc + 1:pc + 6]
            assert 0 <= lv < level.level_count(episode)
            assert nxt == 0 or nxt < len(sections)
            assert 1 <= song <= 41
            assert name < len(names)
            seen.append(lv)
            size = 6
        elif op == script.OP_CHOOSE:
            count = ops[pc + 1]
            assert count <= 4
            for i in range(count):
                assert 0 < ops[pc + 3 + 2 * i] < len(sections)
            size = 2 + 2 * count
        elif op == script.OP_SHOP:
            assert ops[pc + 1] < len(shops)
            size = 2
        elif op in (script.OP_JUMP, script.OP_JUMP_IF_EASY, script.OP_JUMP_IF_DIED,
                    script.OP_JUMP_IF_TIMER, script.OP_JUMP_IF_SHIP13):
            assert 0 < ops[pc + 1] < len(sections)
            size = 2
        elif op == script.OP_CUBES:
            from tyrdata.cubes import load
            count = ops[pc + 1]
            assert count <= 4
            assert all(1 <= cube <= len(load(episode)) for cube in ops[pc + 2:pc + 2 + count])
            size = 2 + count
        elif op in (script.OP_CUBE_SET, script.OP_CUBE_ADD):
            assert ops[pc + 1] <= 4
            size = 2
        elif op == script.OP_TEXT:
            assert ops[pc + 1] < len(pages)
            size = 2
        elif op in (script.OP_PICTURE, script.OP_FADE):
            size = 2
        elif op in (script.OP_SHOP_SONG, script.OP_MUSIC):
            assert 1 <= ops[pc + 1] <= 41
            size = 2
        pc += size
    expected = [level.parse_level_cmd(s).lvl_file_num - 1
                for s in level.load_script(episode) if s.startswith("]L")]
    assert seen == expected

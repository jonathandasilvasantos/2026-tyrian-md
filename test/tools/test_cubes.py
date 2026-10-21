"""Original datacube availability/text, wrapping, highlighting and packed tiles."""
import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'tools'))
from tyrdata import cubes, level, script, shapes  # noqa: E402
from tyrdata.story import text_width  # noqa: E402


@pytest.fixture(scope='module')
def glyphs():
    return shapes.decode_sprite_table(shapes.main_shape_tables()[2])


def test_original_wrapping_and_paragraphs(glyphs):
    assert cubes.wrap(['one two', 'three', '', 'four', '', '', 'five'], glyphs) == [
        ' one two three', ' four', '', '', '', '', ' five']
    lines = cubes.wrap(['this message contains enough words to wrap onto several lines of text'], glyphs)
    assert all(len(line) <= 35 and text_width(line, glyphs) <= 150 for line in lines)
    assert ' '.join(' '.join(lines).split()) == 'this message contains enough words to wrap onto several lines of text'


@pytest.mark.parametrize('episode', [1, 2, 3, 4])
def test_every_script_cube_has_original_text(episode, glyphs):
    records = cubes.load(episode)
    for source in level.load_script(episode):
        if source.startswith(']?'):
            count = script.atoi(source[4:])
            for i in range(count):
                record = records[script.atoi(source[7 + i * 4:]) - 1]
                assert record['title'] and record['body']
                assert -1 <= record['face'] < 12
                lines = cubes.wrap(record['body'], glyphs)
                assert 0 < len(lines) <= 90
                assert all(len(line) <= 35 and text_width(line, glyphs) <= 150 for line in lines)


def test_highlight_and_pixel_bounds(glyphs):
    normal = cubes.line_bitmap('test', glyphs)
    highlighted = cubes.line_bitmap('~test~', glyphs)
    assert np.array_equal(normal == 15, highlighted == 13)
    assert set(np.unique(highlighted)) <= {0, 13}
    assert normal.shape == (8, 152)


@pytest.mark.parametrize('bits', [12, 13, 14, 15, 16])
def test_packed_tiles_extract_at_every_byte_alignment(bits):
    values = [0, 1, (1 << bits) - 1, 123, 0, 771, 15, 2, 310, 19, 0, 77, 1]
    packed = cubes.pack_ids(values, bits)
    for i, expected in enumerate(values):
        bit = i * bits
        word = int.from_bytes(packed[bit // 8:bit // 8 + 3], 'big')
        assert (word >> (24 - bits - bit % 8)) & ((1 << bits) - 1) == expected


def test_cube_conversion_is_deterministic(tmp_path):
    from gen import tiles_column_major
    for directory in ('first', 'second'):
        cubes.generate(str(tmp_path / directory), tiles_column_major)
    for name in ('cubes.h', 'cubes.c', 'cube_graphics.h', 'cube_graphics.c'):
        assert (tmp_path / 'first' / name).read_bytes() == (tmp_path / 'second' / name).read_bytes()


def test_shadow_normalization_preserves_visible_pixels(glyphs):
    from tyrdata.story import draw_text
    reference = np.zeros((224, 320), np.uint8)
    draw_text(reference, 'Tyrian data flux', 1, 0, glyphs)
    # Both transparency and shadow black display black in the reader.
    colors = np.zeros((16, 3), np.uint8)
    colors[15] = 255
    assert np.array_equal(colors[reference[:8, :152]], colors[cubes.line_bitmap('Tyrian data flux', glyphs)])

"""Story conversion preserves original text, proportional layout and screen order."""
import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'tools'))
from tyrdata import level, script, shapes, story  # noqa: E402


@pytest.mark.parametrize('episode,count', [(1, 8), (2, 2), (3, 3), (4, 18)])
def test_original_pages(episode, count):
    lines = level.load_script(episode)
    pages = script.compile_script(episode)[4]
    expected = []
    for i, line in enumerate(lines):
        if line.startswith(']W'):
            body = []
            for text in lines[i + 1:]:
                if text.startswith('#'):
                    break
                body.append(text[:60])
            expected.append(body[:10])
    assert [p['lines'] for p in pages] == expected
    assert len(pages) == count
    glyphs = shapes.decode_sprite_table(shapes.main_shape_tables()[2])
    prompt = script.help_section('** Section 4')[4]
    for page in pages:
        # Source lines fit at their original x=10; no accidental 40-column wrap.
        assert all(story.text_width(text, glyphs) <= 310 for text in page['lines'])
        first = story.page_bitmap(page, glyphs, prompt)
        assert first.shape == (224, 320)
        assert set(np.unique(first)) <= {0, 14, 15}
        assert np.array_equal(first, story.page_bitmap(page, glyphs, prompt))


def test_proportional_advances_and_transparency():
    glyphs = shapes.decode_sprite_table(shapes.main_shape_tables()[2])
    assert story.text_width(' ', glyphs) == 6
    assert story.text_width('ii', glyphs) < story.text_width('WW', glyphs)
    page = dict(lines=['Tyrian'], red=False)
    image = story.page_bitmap(page, glyphs, '')
    assert np.any(image[67:77, 10:50] == 15)
    assert not image[:67].any()


def test_generated_story_is_deterministic(tmp_path):
    from gen import tiles_column_major
    first, second = tmp_path / 'first', tmp_path / 'second'
    story.generate(str(first), tiles_column_major)
    story.generate(str(second), tiles_column_major)
    for name in ('story.h', 'story.c'):
        assert (first / name).read_bytes() == (second / name).read_bytes()

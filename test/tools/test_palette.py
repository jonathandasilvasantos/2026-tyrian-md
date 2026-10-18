import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from tyrdata import palette  # noqa: E402


def test_vga_scaling_spans_full_range():
    assert palette.vga6_to_8(0) == 0
    assert palette.vga6_to_8(63) == 255


def test_cram_roundtrip_of_hardware_levels():
    for r in palette.GENESIS_LEVELS:
        for g in palette.GENESIS_LEVELS:
            rgb = (r, g, 255 - r)
            assert palette.cram_to_rgb(palette.to_cram(rgb))[:2] == (r, g)


@pytest.mark.skipif(not os.path.exists(palette.data_path("palette.dat")), reason="no tyrian-data")
def test_load_all_palettes():
    pals = palette.load_palettes()
    assert len(pals) == palette.PALETTE_COUNT
    assert all(len(p) == 256 for p in pals)
    assert all(0 <= c <= 255 for p in pals for rgb in p for c in rgb)

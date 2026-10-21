"""CRAM inverse matching and deterministic filter resources."""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from tyrdata.filters import make_tables


def test_inverse_palette_endpoints_and_determinism():
    rgb = [(0, 0, 0)] * 256
    rgb[17] = (255, 255, 255)
    inverse, colors = make_tables(rgb)
    assert len(inverse) == 512 and len(colors) == 256
    assert inverse[0] == 0 and inverse[511] == 17
    assert colors[0] == 0 and colors[17] == 0xeee
    assert (inverse, colors) == make_tables(rgb)

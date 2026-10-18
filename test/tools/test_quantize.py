import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from tyrdata import palette, quantize  # noqa: E402

RGB = [(i, (i * 3) % 256, 255 - i) for i in range(256)]
HISTS = [{c: 4 for c in range(s, s + 20)} for s in range(0, 200, 10)]


def test_palettes_fit_the_genesis():
    pals, assign = quantize.quantize_cells(HISTS, RGB)
    assert len(pals) == 2 and len(assign) == len(HISTS)
    for p in pals:
        assert len(p) <= 15
        for c in p:   # every color must survive the CRAM round trip
            assert palette.cram_to_rgb(palette.to_cram(tuple(int(x) for x in c))) == tuple(int(x) for x in c)


def test_deterministic():
    a = quantize.quantize_cells(HISTS, RGB)
    b = quantize.quantize_cells(HISTS, RGB)
    assert all((x == y).all() for x, y in zip(a[0], b[0])) and (a[1] == b[1]).all()


def test_remap_indexes_are_1_based():
    pals, _ = quantize.quantize_cells(HISTS, RGB)
    idx = quantize.remap(np.arange(256), RGB, pals[0])
    assert idx.min() >= 1 and idx.max() <= len(pals[0])

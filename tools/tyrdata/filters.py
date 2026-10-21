"""Inverse palette match for Genesis CRAM filters, deterministic at build time."""
import numpy as np
from . import palette, quantize


def make_tables(rgb):
    lifted = quantize.lift(rgb)
    cram = [palette.cram_to_rgb(((i & 7) << 1) | ((i & 56) << 2) | ((i & 448) << 3)) for i in range(512)]
    inverse = quantize._dist(np.array(cram), lifted).argmin(1).tolist()
    colors = [palette.to_cram(tuple(int(v) for v in color)) for color in lifted]
    return inverse, colors

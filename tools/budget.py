#!/usr/bin/env python3
"""budget.py — Genesis cost of every level's backgrounds (agents.md §3, §9).

Per level: unique 8x8 cells per layer (H/V flips merged) and distinct
colors.  VRAM holds ~1500 usable cells; a level whose layers need more
must stream cells as it scrolls.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from tyrdata import level, raster  # noqa: E402


def main():
    print(f"{'lvl':>6} {'file':12} {'L1':>5} {'L2':>5} {'L3':>5} {'sum':>6} {'colors':>6}")
    tot_cells = 0
    worst = (0, None)
    for ep in range(1, 5):
        for n in range(1, level.level_count(ep) + 1):
            lvl, bmps = raster.load_level_bitmaps(ep, n)
            u = [raster.unique_cells(raster.cells(b)) for b in bmps]
            colors = len(np.unique(np.concatenate([b.ravel() for b in bmps])))
            s = sum(u)
            tot_cells += s
            worst = max(worst, (s, f"{ep}.{n}"))
            print(f"{ep}.{n:<4} {lvl.shape_file:12} {u[0]:5} {u[1]:5} {u[2]:5} {s:6} {colors:6}")
    print(f"total cells {tot_cells} = {tot_cells * 32 // 1024} KB raw 4bpp; worst level {worst[1]} ({worst[0]} cells)")


if __name__ == "__main__":
    main()

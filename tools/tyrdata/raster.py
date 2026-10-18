"""Rasterize level layers into palette-index bitmaps and cut them into
Genesis 8x8 cells (agents.md §4 Backgrounds).

Tyrian map tiles are 24x28: 28 is not a multiple of 8, so cells do not line
up with map rows.  Every 2 map rows (56 px) the grid realigns, so we work on
whole-layer bitmaps and deduplicate cells (with H/V flips) per level.
"""
import numpy as np

from . import level, shapes

TRANSPARENT = 0   # layers 2/3: palette index 0 is transparent (blitBackgroundRow)


def layer_bitmap(lvl, li, tiles):
    """Layer li of a Level -> uint8 array (rows*28, cols*24); blank = 0."""
    grid = lvl.layers[li]
    rows, cols = len(grid), len(grid[0])
    cap = (72, 71, 70)[li]            # varz.h megaData capacities
    bmp = np.zeros((rows * 28, cols * 24), np.uint8)
    for y, row in enumerate(grid):
        for x, idx in enumerate(row):
            rec = lvl.map_sh[li][idx] if idx < cap else 0
            t = tiles[rec - 1] if rec else None
            if t is not None:
                bmp[y * 28:(y + 1) * 28, x * 24:(x + 1) * 24] = np.frombuffer(t, np.uint8).reshape(28, 24)
    return bmp


def cells(bmp):
    """(h, w) bitmap -> (n, 8, 8) cells in row-major order (crops to /8)."""
    h, w = bmp.shape[0] // 8 * 8, bmp.shape[1] // 8 * 8
    b = bmp[:h, :w].reshape(h // 8, 8, w // 8, 8).swapaxes(1, 2)
    return b.reshape(-1, 8, 8)


def unique_cells(cell_arr, flips=True):
    """Number of distinct cells; flips=True merges H/V/HV mirrored copies."""
    seen = set()
    for c in cell_arr:
        if flips:
            key = min(c.tobytes(), c[:, ::-1].tobytes(), c[::-1, :].tobytes(), c[::-1, ::-1].tobytes())
        else:
            key = c.tobytes()
        seen.add(key)
    return len(seen)


def load_level_bitmaps(episode, num):
    lvl = level.load_level(episode, num)
    tiles = shapes.load_tiles(lvl.shape_file)
    return lvl, [layer_bitmap(lvl, li, tiles) for li in range(3)]

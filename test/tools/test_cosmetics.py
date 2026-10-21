"""Packing tiny effects must preserve actual VDP pixels and transparency."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'tools'))
from gen import cosmetic_images, pack_tiny_images, tiles_column_major, GAME_PALETTE, PLAYER_CRAM  # noqa: E402
from tyrdata import palette, quantize  # noqa: E402


def test_packed_vdp_cells_keep_every_original_effect_pixel():
    images = cosmetic_images()
    rgb = palette.load_palettes()[GAME_PALETTE]
    groups, refs = pack_tiny_images(images, rgb)
    colors = [palette.cram_to_rgb(word) for word in PLAYER_CRAM] + [(0, 0, 0), (255, 255, 255)]
    lut = quantize.remap(np.arange(256), rgb, np.array(colors, np.float64))
    def converted(image):
        return np.where(image >= 0, lut[np.maximum(image, 0)], 0)
    assert len(images) == len(refs) == 144
    assert len(groups) < len(images) // 4
    packed = [tiles_column_major(converted(group), 2, 2) for group in groups]
    for image, ref in zip(images, refs):
        words = packed[ref // 4][(ref % 4) * 8:(ref % 4 + 1) * 8]
        cell = np.array([[word >> shift & 15 for shift in range(28, -1, -4)] for word in words])
        assert np.array_equal(cell, converted(image))
    again, again_refs = pack_tiny_images(images, rgb)
    assert again_refs == refs
    assert all(np.array_equal(a, b) for a, b in zip(groups, again))

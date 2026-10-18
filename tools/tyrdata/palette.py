"""palette.dat: 23 palettes x 256 colors x 3 bytes of 6-bit VGA RGB
(OpenTyrian palette.c:loadPals), and their mapping to Genesis CRAM."""
from . import data_path

PALETTE_COUNT = 23

# Measured analog output of the VDP for each 3-bit CRAM level (normal mode),
# the usual reference (Genesis Plus GX / BlastEm); nearest match in this
# space instead of a linear 0..255 split keeps darks from crushing.
GENESIS_LEVELS = (0, 52, 87, 116, 144, 172, 206, 255)


def vga6_to_8(c):
    """6-bit VGA component to 8 bits, as loadPals does."""
    return (c << 2) | (c >> 4)


def load_palettes(path=None):
    """List of 23 palettes, each a list of 256 (r, g, b) 8-bit tuples."""
    with open(path or data_path("palette.dat"), "rb") as f:
        data = f.read()
    if len(data) != PALETTE_COUNT * 256 * 3:
        raise ValueError(f"palette.dat: {len(data)} bytes, expected {PALETTE_COUNT * 768}")
    return [[tuple(vga6_to_8(data[p * 768 + i * 3 + k]) for k in range(3)) for i in range(256)]
            for p in range(PALETTE_COUNT)]


def nearest_level(c):
    """Index 0..7 of the Genesis level closest to an 8-bit component."""
    return min(range(8), key=lambda i: abs(GENESIS_LEVELS[i] - c))


def to_cram(rgb):
    """(r, g, b) 8-bit -> Genesis CRAM word 0000 BBB0 GGG0 RRR0."""
    r, g, b = (nearest_level(c) for c in rgb)
    return (b << 9) | (g << 5) | (r << 1)


def cram_to_rgb(word):
    """CRAM word -> the 8-bit (r, g, b) the VDP shows."""
    return tuple(GENESIS_LEVELS[(word >> s) & 7] for s in (1, 5, 9))

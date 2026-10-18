"""Cell-constrained palette quantization (agents.md §4 Colors).

The Genesis gives each 8x8 cell one 15-color palette line (+ index 0 =
transparent).  Tyrian levels use up to 137 PC colors.  We alternate:
  1. build each palette with weighted k-means over the pixels of the cells
     assigned to it, snapped to the 512 Genesis colors;
  2. reassign every cell to the palette that reproduces it with the least
     error.
Deterministic (fixed init, fixed iteration count), no dithering: dithering
breaks cell deduplication and shimmers when scrolling.
"""
import numpy as np

from . import palette as pal_mod

LEVELS = np.array(pal_mod.GENESIS_LEVELS, np.float64)
# all 512 Genesis colors
GENESIS_RGB = np.array([(r, g, b) for r in LEVELS for g in LEVELS for b in LEVELS])


def oklab(rgb):
    """8-bit sRGB (n, 3) -> OKLab (n, 3).  Distances in OKLab keep hue:
    per-channel RGB snapping turned Tyrian's dark browns red/olive."""
    c = np.asarray(rgb, np.float64) / 255.0
    c = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    lms = c @ np.array([[0.4122214708, 0.2119034982, 0.0883024619],
                        [0.5363325363, 0.6806995451, 0.2817188376],
                        [0.0514459929, 0.1073969566, 0.6299787005]])
    lms = np.cbrt(lms)
    return lms @ np.array([[0.2104542553, 1.9779984951, 0.0259040371],
                           [0.7936177850, -2.4285922050, 0.7827717662],
                           [-0.0040720468, 0.4505937099, -0.8086757660]])


# Lightness counts double against chroma: the Genesis has only 0 and 52 in
# the darks, and a dark brown is better shown as a dark neutral than as
# saturated red (what plain nearest-color picks).
LAB_WEIGHT = np.array([2.0, 1.0, 1.0])
# PC colors are lifted by this gamma before matching: Tyrian's palette is
# dark and the Genesis' first step (52) is coarse.
LIFT_GAMMA = 0.7


def lift(rgb):
    return 255.0 * (np.asarray(rgb, np.float64) / 255.0) ** LIFT_GAMMA


GENESIS_LAB = oklab(GENESIS_RGB)


def snap(rgb):
    """(n, 3) float RGB -> perceptually nearest Genesis color."""
    d = (((oklab(rgb)[:, None, :] - GENESIS_LAB[None]) ** 2) * LAB_WEIGHT).sum(-1)
    return GENESIS_RGB[d.argmin(1)]


def _dist(a, b):
    """Pairwise squared OKLab distance, a (n,3) b (m,3) RGB -> (n,m)."""
    d = oklab(a)[:, None, :] - oklab(b)[None, :, :]
    return (d * d * LAB_WEIGHT).sum(-1)


def _kmeans(colors, weights, k, iters=12):
    """Weighted k-means on a small set of colors; deterministic init by
    farthest-point sampling from the most frequent color."""
    if len(colors) <= k:
        return snap(colors)
    cent = [colors[weights.argmax()]]
    for _ in range(1, k):
        d = _dist(colors, np.array(cent)).min(1) * weights
        cent.append(colors[d.argmax()])
    cent = np.array(cent)
    for _ in range(iters):
        lab = _dist(colors, cent).argmin(1)
        for j in range(k):
            m = lab == j
            if m.any():
                cent[j] = (colors[m] * weights[m, None]).sum(0) / weights[m].sum()
    pal = np.unique(snap(cent), axis=0)
    # snapping merges near centroids: refill the freed slots greedily with
    # the snapped source color that removes the most weighted error
    cand = np.unique(snap(colors), axis=0)
    while len(pal) < k:
        cur = _dist(colors, pal).min(1)
        gain = ((cur[:, None] - _dist(colors, cand)).clip(0) * weights[:, None]).sum(0)
        if gain.max() <= 0:
            break
        pal = np.vstack([pal, cand[gain.argmax()]])
    return pal


def quantize_cells(cell_hists, rgb, n_pals=2, k=15, iters=12):
    """Assign palettes.

    cell_hists: list of dicts {pc_color: pixel_count} per unique cell, with
                cell multiplicity already folded into the counts; the
                transparent color must already be excluded.
    rgb:        (256, 3) PC palette in 8-bit RGB.
    Returns (palettes [n_pals x (<=k, 3)], assignment per cell).
    """
    rgb = lift(rgb)
    # init: k-means of the cells' mean OKLab colors into n_pals groups, so
    # each palette starts on a coherent region (grass, rock, ice...)
    lab = oklab(rgb)
    means = np.array([sum((lab[c] * n for c, n in h.items()), np.zeros(3)) / max(1, sum(h.values()))
                      for h in cell_hists])
    sizes = np.array([sum(h.values()) for h in cell_hists], np.float64)
    cent = [means[sizes.argmax()]]
    for _ in range(1, n_pals):
        cent.append(means[(((means[:, None] - np.array(cent)[None]) ** 2).sum(-1).min(1) * sizes).argmax()])
    cent = np.array(cent)
    for _ in range(10):
        assign = (((means[:, None] - cent[None]) ** 2).sum(-1)).argmin(1)
        for g in range(n_pals):
            m = assign == g
            if m.any():
                cent[g] = (means[m] * sizes[m, None]).sum(0) / sizes[m].sum()
    pals = None
    for _ in range(iters):
        pals = []
        for g in range(n_pals):
            acc = {}
            for h, a in zip(cell_hists, assign):
                if a == g:
                    for c, n in h.items():
                        acc[c] = acc.get(c, 0) + n
            if not acc:
                pals.append(np.zeros((1, 3)))
                continue
            cs = np.array(sorted(acc))
            # sqrt weights: a huge flat area must not take every slot
            pals.append(_kmeans(rgb[cs], np.sqrt(np.array([acc[c] for c in cs], np.float64)), k))
        # reassign by total error
        err = np.zeros((len(cell_hists), n_pals))
        for g, p in enumerate(pals):
            best = _dist(rgb, p).min(1)          # per PC color, error with palette g
            for i, h in enumerate(cell_hists):
                err[i, g] = sum(best[c] * n for c, n in h.items())
        new = err.argmin(1)
        if (new == assign).all():
            break
        assign = new
    return pals, assign


def remap(pc_colors, rgb, pal):
    """PC color indices -> 1-based index into pal (nearest color)."""
    d = _dist(lift(rgb)[np.asarray(pc_colors)], pal)
    return d.argmin(1) + 1

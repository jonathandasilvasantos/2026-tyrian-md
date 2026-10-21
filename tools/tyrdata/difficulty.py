"""OpenTyrian tyrian2.c:JE_makeEnemy and mainint.c:adjust_difficulty.

Float32 arithmetic is reproduced at build time, not in the 68000 loop.
Rows use difficulty -1..10 (index = difficulty + 1).
"""
import numpy as np

ARMOR_FACTORS = (.5, .5, .75, 1, 1.2, 1.5, 1.8, 2, 3, 4, 8, 8)
VALUE_FACTORS = (.75, .75, 1, 1, 1.125, 1.5, 2, 2.5, 4, 4, 8, 8)
SCORE_FACTORS = (.4, .8, 1.3, 1.6, 2, 2, 3, 3, 3)
SCORE_BOUNDS = (40000, 70000, 150000, 300000, 600000, 1000000, 2000000, 3000000)


def enemy_armor(raw, difficulty):
    if raw in (0, 255):
        return raw
    result = np.float32(raw) * np.float32(ARMOR_FACTORS[difficulty + 1])
    if difficulty <= 1:
        result += np.float32(1)
    return min(254, int(result))


def enemy_value(raw, difficulty):
    if not 1 < raw < 10000:
        return raw
    return min(10000, int(np.float32(raw) * np.float32(VALUE_FACTORS[difficulty + 1])))


def adjusted_score(raw, initial):
    # C roundf for a positive float32: ties away from zero, not bankers' round.
    value = np.float32(raw) * np.float32(SCORE_FACTORS[initial - 1])
    return int(np.floor(np.float64(value) + .5))


def score_thresholds(initial):
    result = []
    for boundary in SCORE_BOUNDS:
        lo, hi = 0, 10000000
        while lo < hi:
            mid = (lo + hi) // 2
            if adjusted_score(mid, initial) >= boundary:
                hi = mid
            else:
                lo = mid + 1
        result.append(lo)
    return result

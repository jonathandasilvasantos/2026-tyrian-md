"""PC difficulty rules are baked into ROM without runtime float or division."""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from tyrdata.difficulty import enemy_armor, enemy_value, score_thresholds  # noqa: E402


@pytest.mark.parametrize("difficulty,armor,value", [(-1, 11, 750), (0, 11, 750), (1, 16, 1000),
    (2, 20, 1000), (3, 24, 1125), (4, 30, 1500), (5, 36, 2000), (6, 40, 2500),
    (7, 60, 4000), (8, 80, 4000), (9, 160, 8000), (10, 160, 8000)])
def test_pc_enemy_multipliers(difficulty, armor, value):
    assert enemy_armor(20, difficulty) == armor
    assert enemy_value(1000, difficulty) == value
    assert enemy_armor(255, difficulty) == 255
    assert enemy_value(1, difficulty) == 1
    assert enemy_value(30001, difficulty) == 30001
    assert enemy_value(-200, difficulty) == -200


def test_clamping_and_truncation():
    assert enemy_armor(254, 10) == 254
    assert enemy_armor(5, 1) == 4
    assert enemy_value(9000, 3) == 10000
    assert enemy_value(5, 4) == 7


def test_dynamic_difficulty_rounding_boundaries():
    assert score_thresholds(1)[0] == 99999  # roundf(99999 * 0.4f) == 40000
    assert score_thresholds(2)[:2] == [50000, 87500]
    assert score_thresholds(8)[:2] == [13334, 23334]

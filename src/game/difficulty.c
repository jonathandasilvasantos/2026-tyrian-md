#include "difficulty.h"
#include "game.h"
#include "shop.h"
#include "gen_data/difficulty.h"

uint8_t difficulty_for_score(uint32_t score, uint8_t initial)
{
    const uint32_t *bounds;
    uint8_t result = DIFFICULTY_EASY, i;
    if (initial < 1) initial = 1;
    if (initial > 9) initial = 9;
    bounds = score_thresholds + (initial - 1) * 8;
    for (i = 0; i < 8 && score >= bounds[i]; i++) result++;
    return result;
}

void game_level_complete(void)
{
    uint8_t next = difficulty_for_score(player_total_value(), game.initial_difficulty);
    /* JE_main restores oldDifficultyLevel before JE_endLevelAni adjusts it:
     * event-46 changes affect only their level, not the campaign baseline. */
    if (next > game.base_difficulty) game.base_difficulty = next;
    game.difficulty = game.base_difficulty;
}

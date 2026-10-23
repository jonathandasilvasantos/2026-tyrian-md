/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
#include "episode.h"
#include "game.h"
#include "player.h"
#include "rng.h"
#include "mode.h"
#include "script.h"
#include "gen_data/episodes.h"
#include "gen_data/level.h"

void game_new(uint8_t episode, uint8_t difficulty)
{
    if (episode < 1 || episode > EPISODE_COUNT) episode = LEVEL_DEFAULT_EPISODE;
    if (difficulty < DIFFICULTY_EASY || difficulty > DIFFICULTY_10) difficulty = DIFFICULTY_NORMAL;
    episode_tables_select(episode);
    game.initial_difficulty = game.base_difficulty = difficulty;
    game.initial_episode = episode;
    game.repeated = 0;
    mode_reset();
    rng_seed(0x2545F491u);
    player_init();
    game_start_level(LEVEL_FIRST);
    script_start();
}

void game_next_episode(void)
{
    uint8_t next = LEVEL_EPISODE + 1;
    if (next > EPISODE_COUNT) {
        next = 1;
        game.repeated = 1;
        /* JE_nextEpisode: the campaign may reward the SuperCarrot on wrap. */
        if (rng_mod(6) == 0) {
            Player *p = &players[0];
            p->ship = 2;
            p->weapon_id[FRONT_WEAPON] = 23;
            p->weapon_id[REAR_WEAPON] = 24;
            p->weapon_power[0] = p->weapon_power[1] = 1;
        }
    }
    mode_leave_level();
    episode_tables_select(next);
    script_start();
}

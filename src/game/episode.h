/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
/* Story campaign state; OpenTyrian mainint.c:JE_nextEpisode. */
#ifndef GAME_EPISODE_H
#define GAME_EPISODE_H
#include "types.h"
/* New game resets equipment/funds; advancing preserves both and RNG. */
void game_new(uint8_t episode, uint8_t difficulty);
void game_next_episode(void);
#endif

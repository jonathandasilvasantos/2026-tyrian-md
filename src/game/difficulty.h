/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
/* Story difficulty scaling and end-of-level score progression (PC rules). */
#ifndef GAME_DIFFICULTY_H
#define GAME_DIFFICULTY_H
#include "types.h"
uint8_t difficulty_for_score(uint32_t score, uint8_t initial);
void game_level_complete(void);
#endif

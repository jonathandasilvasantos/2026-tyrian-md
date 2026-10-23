/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
/* Companions: mainint.c JE_playerMovement and varz.c JE_drawOptions. */
#ifndef GAME_SIDEKICK_H
#define GAME_SIDEKICK_H
#include "types.h"
void sidekicks_refresh(void); /* JE_drawOptions: refill/reinitialize both options */
void sidekicks_init(void);
void sidekicks_before_move(void);
void sidekicks_record_move(int16_t old_x, int16_t old_y);
void sidekicks_tick(uint16_t pad);
void sidekicks_animate(void);
#endif

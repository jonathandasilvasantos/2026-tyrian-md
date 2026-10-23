/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * See LICENSE and NOTICE.
 */
/* bg.h — streaming background layers (agents.md §4 Backgrounds). */
#ifndef BG_H
#define BG_H

#include <genesis.h>

#include "gen_data/level_bg.h"
#include "game/stars.h"

/* first VRAM tile after the background slot pool */
#include "game/game.h"
#define BG_VRAM_END (TILE_USER_INDEX + level_bgs[game.level].pool_need)

void bg_init(void);
/* Coarse star occlusion: both planes must have a fully clear 8x8 cell. */
void bg_star_masks(StarMask masks[2]);
/* once per 60 Hz frame; frac = progress towards the next tick, 0..11 */
void bg_update(u16 frac, bool light);

#endif

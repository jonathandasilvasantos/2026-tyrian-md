/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
/* shots.h — player shots (OpenTyrian shots.c). */
#ifndef GAME_SHOTS_H
#define GAME_SHOTS_H

#include "types.h"

/* PC: MAX_PWEAPON 81 (slot 80 reserved).  Genesis cap, see agents.md §9. */
#define PLAYER_SHOT_MAX 64

typedef struct {
    int16_t x, y, xm, ym;          /* shotX/Y, shotXM/YM */
    int16_t px, py;                /* position at the previous tick (render interpolation) */
    int8_t xc, yc;                 /* shotXC/YC */
    int16_t dev_x, dev_y;          /* circle shots */
    int8_t dir_x, dir_y;
    uint8_t cir_x, cir_y, complicated;
    uint8_t ani, ani_max, trail, blast_filter, chain, player;
    uint8_t aim_at, aim_delay, aim_delay_max;
    uint8_t avail;                 /* 0 free, else ticks left */
    int16_t dmg;
    uint16_t gr;                   /* shotGr */
    uint16_t frame;                /* sprite frame drawn this tick (1..1000), 0 = not drawn yet */
} PlayerShot;

extern PlayerShot player_shots[PLAYER_SHOT_MAX];

void shots_init(void);
/* player_shot_create: returns the slot or PLAYER_SHOT_MAX. */
int shot_create(uint16_t port, uint8_t bay, int16_t px, int16_t py, uint16_t wpn, uint8_t player_num);
void shots_move(void);

#endif

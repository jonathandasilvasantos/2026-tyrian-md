/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
/* enemy.h — enemies, enemy shots and explosions (OpenTyrian tyrian2.c
 * JE_drawEnemy/JE_makeEnemy/JE_newEnemy, varz.c JE_setupExplosion*). */
#ifndef GAME_ENEMY_H
#define GAME_ENEMY_H

#include "types.h"

#define ENEMY_MAX 100            /* 4 groups of 25: sky 0, ground 25, top 50, ground2 75 */
#define ENEMY_SHOT_MAX 60
#define EXPLOSION_MAX 64         /* PC: 200, see agents.md §9 */
#define REP_EXPLOSION_MAX 20

/* enemyAvail values */
#define EA_ACTIVE 0
#define EA_FREE 1
#define EA_ITEM 2                /* collectible, not shootable */

typedef struct {
    int16_t ex, ey;
    int8_t exc, eyc;             /* speed */
    int8_t excc, eycc;           /* fixed acceleration wait */
    int8_t exccw, eyccw;
    int8_t exrev, eyrev;
    int8_t exccadd, eyccadd;
    uint8_t exccwmax, eyccwmax;
    uint8_t armorleft;
    uint8_t eshotwait[3], eshotmultipos[3];
    uint8_t enemycycle, ani, animin, animax, aniactive, aniwhenfire;
    uint8_t size, linknum, shapebank;
    uint8_t xaccel, yaccel;
    uint8_t tur[3], freq[3];
    uint8_t launchwait, launchfreq, launchspecial;
    uint16_t launchtype;
    uint8_t edamaged, enemyground, explonum, scoreitem;
    uint8_t special, flagnum, setto, iced, filter;
    int8_t edlevel, edani;
    uint16_t edgr, enemytype, enemydie;
    int16_t evalue, fixedmovey, mapoffset;
    int16_t xminbounce, xmaxbounce, yminbounce, ymaxbounce;
    uint16_t egr[20];
    int16_t px, py;              /* screen position at the previous tick (render interpolation) */
    uint8_t moved;               /* updated this tick (interpolate from px/py) */
} Enemy;

typedef struct {
    int16_t sx, sy, sxm, sym;
    int8_t sxc, syc;
    uint8_t tx, ty;
    uint16_t sgr;
    uint8_t sdmg, duration, animate, animax;
    uint8_t avail;               /* 1 free (enemyShotAvail), 0 live */
    int16_t px, py;
} EnemyShot;

typedef struct {
    uint8_t ttl;
    int16_t x, y;
    uint16_t sprite;
    uint8_t follow_player, fixed_position;
    int16_t delta_y;
    int16_t px, py;
} Explosion;

typedef struct {
    uint8_t delay, ttl, big;
    int16_t x, y;
} RepExplosion;

extern Enemy enemies[ENEMY_MAX];
extern uint8_t enemy_avail[ENEMY_MAX];
/* 4 free slots from i (i % 4 == 0): skip them with one 32-bit read */
#define ENEMY_QUAD_FREE(i) (((i) & 3) == 0 && *(const uint32_t *)&enemy_avail[i] == 0x01010101u)
extern EnemyShot enemy_shots[ENEMY_SHOT_MAX];
extern Explosion explosions[EXPLOSION_MAX];
extern RepExplosion rep_explosions[REP_EXPLOSION_MAX];
extern uint8_t enemy_bank_ids[4];   /* shape banks loaded by event 5 */
extern uint16_t enemy_spawns;       /* bumped by every enemy_make (invalidates lists) */

void enemies_init(void);
/* JE_newEnemy: first free slot of the group; returns slot+1 or 0. */
int enemy_new(int offset, uint16_t edat, int16_t unique_bank);
uint8_t enemy_make(Enemy *e, uint16_t edat, int16_t unique_bank);
/* JE_drawEnemy (movement, firing, launching) for one group. */
void enemies_update(int offset, int16_t map_x_ofs, int16_t back_move);
void enemy_shots_update(void);
void explosion_setup(int16_t x, int16_t y, int16_t delta_y, int16_t type, uint8_t fixed, uint8_t follow);
void explosion_setup_large(uint8_t ground, uint8_t explo_num, int16_t x, int16_t y);
void explosions_update(void);

#endif

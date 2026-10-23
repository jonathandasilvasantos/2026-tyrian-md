/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
/* JE_SFCodes (mainint.c), fixed ship/key tables from OpenTyrian varz.c.
 * Direction symbols are sampled before inertia/clipping, as on the PC. */
#include "special.h"
#include "player.h"
#include "mode.h"
#include "gen_data/items.h"

static const uint8_t keys[26][8] = {
    {2,1,2,5,137}, {4,3,2,5,138}, {3,4,6,139}, {2,5,142}, {6,2,6,143},
    {6,7,5,8,6,7,5,112}, {7,8,101}, {1,7,6,146}, {8,6,7,1,120},
    {3,6,8,5,121}, {1,2,7,8,119}, {3,4,3,6,123}, {6,7,5,8,124},
    {1,6,125}, {9,5,126}, {1,7,127}, {1,8,128}, {9,7,129}, {9,8,130},
    {4,2,3,5,131}, {3,1,2,8,132}, {2,4,5,133}, {3,4,2,8,134},
    {1,4,6,135}, {1,3,6,137}, {1,4,3,4,7,136}
};
static const uint8_t ship_combos[14][3] = {
    {5,4,7}, {1,2,0}, {14,4,0}, {4,5,0}, {6,5,0}, {7,8,0}, {7,9,0},
    {10,3,5}, {5,8,9}, {1,3,0}, {7,16,17}, {2,11,12}, {3,8,10}, {0,0,0}
};

static const uint8_t super_combos[21] = {
    15,16,17,18,19,20,21,22,23,24,7,8,5,25,14,4,6,3,9,2,26
};

void special_combo(uint8_t symbol)
{
    SpecialState *st = &special_state;
    uint8_t ship = players[0].ship, i;
    if (ship >= 14 || !symbol || symbol > 9) return;
    for (i = 0; i < (game_mode.super_tyrian ? 21 : 3); i++) {
        uint8_t combo = game_mode.super_tyrian ? super_combos[i] : ship_combos[ship][i];
        uint8_t pos = st->combo[i], next;
        if (!combo) { st->combo[i] = 0; continue; }
        next = keys[combo - 1][pos];
        if (next == symbol) {
            next = keys[combo - 1][++st->combo[i]];
            if (next > 100 && next <= 100 + SPECIAL_NUM) {
                st->combo[i] = 0;
                st->executed = next - 100;
            }
        } else if (symbol != 9 && (next - 1) % 4 != (symbol - 1) % 4 &&
                   (!pos || keys[combo - 1][pos - 1] != symbol)) st->combo[i] = 0;
    }
}

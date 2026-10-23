/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
#include "stars.h"
#include "special.h"
#include "rng.h"
Starfield starfield;

void stars_reset(void)
{
    starfield.speed = 1; starfield.active = 1;
    starfield.initialized = starfield.draw_astral = 0;
}

void stars_begin(void)
{
    int i;
    if (starfield.initialized) return;
    /* Campaign/menu preparation may restart a level before play, and must
     * not consume the PC's 400 random draws twice. The ROM initializes during
     * loading; the host can defer this until its first tick, before events. */
    for (i = STAR_COUNT - 1; i >= 0; i--) {
        Star *s = &starfield.star[i];
        uint16_t x = rng_mod(320), y = rng_mod(200);
        s->position = x + y * 320;
        s->speed = rng_mod(3) + 2;
        s->color = rng_mod(16) + 0x90;
    }
    starfield.initialized = 1;
}

void stars_update(void)
{
    uint16_t delta[3];
    int i;
    if (!starfield.initialized) return;
    starfield.draw_astral = special_state.astral > 0;
    if (!starfield.active && !starfield.draw_astral) return;
    for (i = 0; i < 3; i++) delta[i] = (starfield.speed + i + 2) * 320;
    for (i = STAR_COUNT - 1; i >= 0; i--)
        starfield.star[i].position += delta[starfield.star[i].speed - 2];
}

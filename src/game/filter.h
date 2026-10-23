/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
/* OpenTyrian event 44 / JE_filterScreen, at simulation rate. */
#ifndef GAME_FILTER_H
#define GAME_FILTER_H
#include "types.h"
typedef struct {
    int8_t hue, brightness, next_hue, delta, draw_hue, draw_brightness;
    uint8_t active, fade, fade_start;
} FilterState;
extern FilterState filter_state;
void filter_init(void);
void filter_tick(void);
uint8_t filter_index(uint8_t color, int8_t hue, int8_t brightness);
#endif

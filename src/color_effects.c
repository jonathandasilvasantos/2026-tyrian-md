/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * See LICENSE and NOTICE.
 */
/* Per-pixel PC index filters become CRAM changes. Quantization has already
 * merged some PC colors; use a build-time inverse match for each CRAM word.
 * Keep PAL2 text black/white so the window HUD remains legible. */
#include <genesis.h>
#include "color_effects.h"
#include "game/filter.h"
#include "game/game.h"
#include "gen_data/filters.h"
#include "gen_data/level_bg.h"
#include "gen_data/sprites.h"

static u16 colors[64], shown;
void color_effects_reset(void) { shown = 0xffff; }

void color_effects_update(void)
{
    s8 hue = filter_state.draw_hue, brightness = filter_state.draw_brightness;
    u16 key = (u16)(u8)hue << 8 | (u8)brightness, i;
    if (shown == key) return;
    shown = key;
    for (i = 0; i < 64; i++) {
        u16 word = i < 32 ? level_bgs[game.level].palette[i] : spr_palettes[i - 32];
        if ((hue != -99 || (brightness != -99 && brightness != 0)) && i != 46 && i != 47) {
            u16 packed = (word & 14) >> 1 | (word & 224) >> 2 | (word & 3584) >> 3;
            word = filtered_palette[filter_index(cram_pc_index[packed], hue, brightness)];
        }
        colors[i] = word;
    }
    PAL_setColors(0, colors, 64, DMA_QUEUE_COPY);
}

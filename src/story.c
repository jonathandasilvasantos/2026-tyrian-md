/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * See LICENSE and NOTICE.
 */
#include "story.h"
#include "menu.h"
#include "dmaq.h"
#include "gen_data/story.h"
#include "gen_data/level.h"
#include "gen_data/sound_table.h"

static const StoryPicture *picture;
static u8 skipped;

/* Fixed-region maps are CPU-readable; only tile payloads occupy mapper banks.
 * A single row lives on the stack and is consumed by immediate DMA. */
static void load_map(VDPPlane plane, const u16 *map, u16 base)
{
    u16 row[40], x, y;
    for (y = 0; y < 28; y++) {
        for (x = 0; x < 40; x++) row[x] = map[y * 40 + x] + base;
        VDP_setTileMapDataRow(plane, row, y, 0, 40, DMA);
    }
}

u16 story_background(u16 base)
{
    if (!picture) return base;
    PAL_setColors(0, picture->palette, 32, DMA);
    PAL_setColors(48, picture->palette + 48, 16, DMA);
    dmaq_vram(picture->tiles, base * 32, picture->count * 16);
    dmaq_flush();             /* loading with display off; never a gameplay DMA */
    load_map(BG_B, picture->map, base);
    return base + picture->count;
}

void story_reset(void)
{
    picture = NULL;
    skipped = 0;
}

void story_resume(void)
{
    skipped = 0;              /* ]n clears the PC's ESCPressed scene latch */
}

void story_picture(u8 number)
{
    u16 i;
    picture = NULL;
    for (i = 0; i < 6; i++)
        if (story_pictures[i].number == number) picture = &story_pictures[i];
    {
        u16 target[64];
        menu_story_begin();
        PAL_getColors(0, target, 64);
        PAL_setColors(0, palette_black, 64, DMA);
        VDP_setEnable(TRUE);
        PAL_fadeInAll(target, 10, FALSE);
    }
}

void story_text(u8 page)
{
    const StoryPage *text = &story_pages[LEVEL_EPISODE - 1][page];
    u16 base, edge, clock = 0, phase = 5, sound_delay = 0;
    s16 direction = 1;
    static const u32 bar[8] = {0xDDDDDDDD,0xDDDDDDDD,0xDDDDDDDD,0xDDDDDDDD,
                               0xDDDDDDDD,0xDDDDDDDD,0xDDDDDDDD,0xDDDDDDDD};
    if (skipped || !text->count) return;
    base = menu_story_begin();
    dmaq_vram(text->tiles, base * 32, text->count * 16);
    dmaq_flush();
    load_map(BG_A, text->map, base);
    if (text->warning) {
        u16 tile = base + text->count;
        VDP_loadTileData(bar, tile, 1, DMA);
        /* Six-pixel PC warning bars round to eight-pixel VDP rows. */
        VDP_fillTileMapRect(BG_A, TILE_ATTR_FULL(PAL2, TRUE, FALSE, FALSE, tile), 0, 1, 40, 1);
        VDP_fillTileMapRect(BG_A, TILE_ATTR_FULL(PAL2, TRUE, FALSE, FALSE, tile), 0, 26, 40, 1);
    }
    VDP_setEnable(TRUE);
    for (;;) {
        edge = menu_pad_edge();
        if (edge & BUTTON_C) { skipped = 1; break; }
        if (edge & (BUTTON_A | BUTTON_B | BUTTON_START)) break;
        /* PC's six 69.57-Hz timer ticks, normalized for PAL/NTSC display. */
        clock += 70;
        if (text->warning && clock >= (IS_PAL_SYSTEM ? 300 : 360)) {
            clock -= IS_PAL_SYSTEM ? 300 : 360;
            phase += direction;
            if (phase > 10 || phase < 4) direction = -direction;
            PAL_setColor(45, (phase > 7 ? 0x000E : 0x0008));
            if (sound_delay) sound_delay--;
            else {
                sound_delay = 14;
                XGM2_playPCMEx(sound_defs[17].data, sound_defs[17].len, SOUND_PCM_CH3,
                              15, sound_defs[17].half_rate, FALSE);
            }
        }
        menu_frame();
    }
}

void story_fade(u8 type)
{
    if (type == 2) {
        u16 white[64], i;
        for (i = 0; i < 64; i++) white[i] = 0x0EEE;
        PAL_fadeToAll(white, 100, FALSE);
    }
    PAL_fadeOutAll(type == 2 ? 30 : 10, FALSE);
    if (type) {
        picture = NULL;
        menu_story_begin();
        VDP_setEnable(TRUE);
    }
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * See LICENSE and NOTICE.
 */
/* Datacube reader: original 150-pixel wrapping and faces, nine visible lines.
 * Five 32x8 sprites per line retain the PC's 12-pixel spacing and pixel
 * coordinates (plus 12 screen lines). Two VRAM pages keep old text intact
 * during DMA, capped at 48 tiles/vblank. No framebuffer/heap buffer. */
#include "menu.h"
#include "dmaq.h"
#include "game/cubes.h"
#include "gen_data/cube_graphics.h"

#define CUBE_COLUMNS 20
#define CUBE_ROWS 9
#define CUBE_PAGE_TILES (CUBE_COLUMNS * CUBE_ROWS)
#define CUBE_TEXT_BASE (TILE_USER_INDEX + 192)
#define CUBE_FACE_BASE (CUBE_TEXT_BASE + 2 * CUBE_PAGE_TILES)
_Static_assert(CUBE_FACE_BASE + 63 < 1440, "datacube VRAM exceeds tile space");

static u16 loaded[2][CUBE_PAGE_TILES];

static void reader_header(const CubeDef *cube, u8 slot)
{
    char title[81], status[9] = "DATA 1/4";
    u16 n = 0, i, row;
    for (i = 0; cube->title[i] && n < 80; i++)
        if (cube->title[i] != '~') title[n++] = cube->title[i];
    title[n] = 0;
    for (row = 0; row < 3; row++)
        menu_text(1, row, row * 38 < n ? title + row * 38 : "", 38);
    status[5] = '1' + slot;
    status[7] = '0' + cube_available_count();
    menu_text(2, 3, status, 16);
    menu_text(2, 10, cube->header, 16);
    menu_text(1, 13, "U/D scroll  L/R cube  C back", 38);
}

static void reader_face(const CubeDef *cube)
{
    u16 x, y, row[9];
    const CubeFace *face = cube->face && cube->face <= 12 ? &cube_faces[cube->face - 1] : NULL;
    if (face) {
        PAL_setColors(48, face->palette, 16, DMA_QUEUE);
        dmaq_vram(face->tiles, CUBE_FACE_BASE * 32, 63 * 16);
    }
    for (y = 0; y < 7; y++) {
        for (x = 0; x < 9; x++) row[x] = face ? TILE_ATTR_FULL(PAL3, FALSE, FALSE, FALSE,
                                                       CUBE_FACE_BASE + y * 9 + x) : 0;
        VDP_setTileMapDataRow(BG_B, row, 8 + y, 5, 9, DMA_QUEUE_COPY);
    }
}

static void reader_view(const CubeDef *cube, u8 top, u8 page)
{
    u16 y, x, pending = 0;
    char progress[16];
    uintToStr(cube->lines <= CUBE_ROWS ? 100 : (u16)top * 100 / (cube->lines - CUBE_ROWS), progress, 1);
    strcat(progress, "% read");
    menu_text(2, 11, progress, 16);
    u16 base = CUBE_TEXT_BASE + page * CUBE_PAGE_TILES;
    for (y = 0; y < CUBE_ROWS; y++) {
        u16 line = top + y < cube->lines ? cube_lines[cube->first_line + top + y] : 0;
        for (x = 0; x < CUBE_COLUMNS; x++) {
            u16 slot = y * CUBE_COLUMNS + x, tile = x < 19 ? cube_tile(line, x) : 0;
            if (loaded[page][slot] == tile) continue;
            loaded[page][slot] = tile;
            dmaq_vram(cube_text_tiles + (u32)tile * 8, (base + slot) * 32, 16);
            if (++pending == 48) { menu_frame(); pending = 0; }
        }
    }
    if (pending) menu_frame();
    for (y = 0; y < CUBE_ROWS; y++) {
        for (x = 0; x < 5; x++) {
            u16 index = y * 5 + x;
            VDP_setSpriteFull(index, 160 + x * 32, 62 + y * 12, SPRITE_SIZE(4, 1),
                              TILE_ATTR_FULL(PAL2, FALSE, FALSE, FALSE, base + y * CUBE_COLUMNS + x * 4),
                              index == 44 ? 0 : index + 1);
        }
    }
    VDP_updateSprites(45, DMA_QUEUE);
    menu_frame();   /* swap all nine lines together, after tiles are resident */
}

void cube_screen(void)
{
    u8 slot = 0, top = 0, page = 0, redraw = 2, repeat = 0;
    u16 held = 0;
    const CubeDef *cube;
    if (!cube_available_count()) return;
    menu_reader_begin();
    PAL_setColor(45, 0x00EE);  /* ~highlight~: white becomes yellow on Genesis */
    memset(loaded, 0xFF, sizeof(loaded));
    for (;;) {
        u16 edge, pad;
        cube = cube_available(slot);
        if (!cube) break;
        if (redraw) {
            if (redraw == 2) { reader_header(cube, slot); reader_face(cube); }
            page ^= 1;
            reader_view(cube, top, page);
            VDP_setEnable(TRUE);
            redraw = 0;
        }
        edge = menu_pad_edge();
        pad = JOY_readJoypad(JOY_1) & (BUTTON_UP | BUTTON_DOWN);
        if (pad && pad == held) {
            if (repeat) repeat--;
            else { edge |= pad; repeat = IS_PAL_SYSTEM ? 4 : 5; }
        } else repeat = IS_PAL_SYSTEM ? 15 : 18;
        held = pad;
        if (edge & (BUTTON_C | BUTTON_B | BUTTON_START | BUTTON_A)) break;
        if ((edge & BUTTON_UP) && top) { top--; redraw = 1; }
        if ((edge & BUTTON_DOWN) && top + CUBE_ROWS < cube->lines) { top++; redraw = 1; }
        if (edge & (BUTTON_LEFT | BUTTON_RIGHT)) {
            u8 count = cube_available_count();
            slot = edge & BUTTON_LEFT ? (slot ? slot - 1 : count - 1) : (slot + 1 < count ? slot + 1 : 0);
            top = 0;
            redraw = 2;
        }
        menu_frame();
    }
}

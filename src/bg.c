/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * See LICENSE and NOTICE.
 */
/* bg.c — streaming background layers.
 *
 * Layer 1 is drawn on plane B, layer 2 on plane A (64x32-cell planes).
 * Positions come from the game state (tyrian2.c's mapYPos/backPos and the
 * parallax offsets), interpolated between ticks like the sprites, so
 * ground enemies stay locked to the map.  Scrolling is kept continuous
 * across map wraps/jumps (LayerScroll.wrap_rows); only even row counts
 * keep the 8-px cell grid (odd ones would need a second cell set: no level
 * converted so far uses them).  Only
 * the cells of the BG_WIN_ROWS rows around the screen live in VRAM: a cell
 * gets a tile slot from a shared pool when the first resident row uses it
 * and returns it when the last one leaves (reference counts per cell).
 * Rows enter at the top as the level scrolls up.  Bringing a row in is
 * split into 3 steps, one per frame for both layers together: release the
 * row that left the window, load the left half, load the right half and
 * write the tilemap row.  The one-row margin above the screen hides the
 * delay (a row lasts >= 6 frames at backMove 2), and the per-frame cost and
 * DMA (<= 21 tiles = 672 bytes) stay small.
 */
#include "bg.h"
#include "game/game.h"
#include "game/level.h"
#include "game/stars.h"
#include "gen_data/level_bg.h"
#include "dmaq.h"

#define PLANE_ROWS 32
#define ROW_CELLS_MAX 48
/* Slot pool: everything the worst window of this level needs.  The rest of
 * VRAM below the tilemaps stays free for sprites, HUD and font. */
#define POOL_SIZE BG_POOL_NEED
/* PC playfield (24..287 x 0..183) -> screen; sprites.c uses the same. */
#define SX_OFF (-16)
#define SY_OFF 24

typedef struct {
    const BgLayer *def;
    VDPPlane plane;
    u16 *slot;          /* cell -> VRAM tile index */
    u16 *ref;           /* cell -> resident uses */
    s16 vy_prev, vy_cur;/* virtual layer y at the screen top, previous / current tick */
    s16 x_prev, x_cur;  /* horizontal scroll, previous / current tick */
    s16 vy;             /* interpolated, this frame */
    s16 top;            /* first resident (virtual) cell row */
    u16 rows;           /* cell rows in the layer */
    s16 loading;        /* row being brought in, -1 none */
    u16 step;           /* next step for it: 0 release, 1 left half, 2 right half */
    u16 row_buf[ROW_CELLS_MAX];
    u8 star_buf[ROW_CELLS_MAX];
} Layer;

static Layer layers[BG_LAYER_COUNT];
static u8 star_clear[BG_LAYER_COUNT][32][ROW_CELLS_MAX];
static s16 rendered_x[BG_LAYER_COUNT];
static bool astral_hidden;
static u16 back_shadow[PLANE_ROWS][64]; /* bulk Astral restore, no row rebuild */
static u32 last_tick;
/* real row each plane row was loaded from: release must decode the same
 * cells even after a wrap moved the mapping */
static s16 loaded_real[BG_LAYER_COUNT][32];
static u16 free_slots[POOL_SIZE];
static u16 free_count;

/* The layers' per-cell slot and reference tables, carved from one block
 * (4 bytes per cell: 5.8 KB for level 1.9, see agents.md §9). */
static u16 cell_tables[2 * BG_CELLS_TOTAL];

/* Real cell row of virtual cell row v (wraps moved the map pointer by
 * wrap_rows map rows = 3.5 cells each). */
static s16 real_row(const Layer *l, s16 v)
{
    s16 r = v - (s16)((game.layer[l - layers].wrap_rows * 7) >> 1);
    if (r < 0) r = 0;
    if (r >= (s16)l->rows) r = l->rows - 1;
    return r;
}

/* Load the cells of metatile columns [mx0, mx1) of virtual row v. */
static void load_cols(Layer *l, s16 v, u16 mx0, u16 mx1)
{
    const BgLayer *d = l->def;
    s16 r = real_row(l, v);
    const u16 *mm = d->metamap + (u16)(r / 7) * d->mcols;
    u16 sub = (r % 7) * 3, mx, k;
    u16 *out = l->row_buf + mx0 * 3;

    for (mx = mx0; mx < mx1; mx++) {
        const u16 *m = d->meta + mm[mx] * 21 + sub;
        for (k = 0; k < 3; k++) {
            u16 a = m[k], cell = a & 0x7FF;
            if (l->ref[cell]++ == 0) {
                if (!free_count) SYS_die("bg: VRAM slot pool empty");
                l->slot[cell] = free_slots[--free_count];
                dmaq_vram(d->tiles + cell * 8, l->slot[cell] * 32, 16);
            }
            *out++ = (a & 0xF800) | l->slot[cell];
            l->star_buf[mx * 3 + k] = d->star_clear[cell];
        }
    }
}

static void write_row(Layer *l, s16 v, TransferMethod tm)
{
    loaded_real[l - layers][v & 31] = real_row(l, v);
    static const u16 empty[ROW_CELLS_MAX] = {0};
    u16 width = l->def->mcols * 3;
    memcpy(star_clear[l - layers][v & 31], l->star_buf, width);
    if (l == layers) memcpy(back_shadow[v & 31], l->row_buf, width * 2);
    VDP_setTileMapDataRow(l->plane, astral_hidden && l == layers ? empty : l->row_buf,
                         v & (PLANE_ROWS - 1), 0, width, tm);
}

/* Virtual y of the screen top and hscroll for layer i from the game. */
static void layer_target(u16 i, s16 *vy, s16 *x)
{
    const LayerScroll *s = &game.layer[i];
    *vy = (level_layer_row(i) + s->wrap_rows) * 28 - s->back_pos - SY_OFF;
    *x = game.map_x_ofs[i] - 48 + SX_OFF;    /* screen = layer_x - 48 + ofs */
}

static void release_row(Layer *l, s16 v)
{
    const BgLayer *d = l->def;
    s16 r = loaded_real[l - layers][v & 31];
    const u16 *mm = d->metamap + (u16)(r / 7) * d->mcols;
    u16 sub = (r % 7) * 3, mx, k;

    for (mx = 0; mx < d->mcols; mx++) {
        const u16 *m = d->meta + mm[mx] * 21 + sub;
        for (k = 0; k < 3; k++) {
            u16 cell = m[k] & 0x7FF;
            if (--l->ref[cell] == 0) free_slots[free_count++] = l->slot[cell];
        }
    }
}

/* Wanted first resident row: one row of margin above the screen. */
static s16 want_top(const Layer *l)
{
    s16 t = (l->vy >> 3) - 1;
    return t < 0 ? 0 : t;
}

void bg_init(void)
{
    static const VDPPlane planes[] = { BG_B, BG_A };
    u16 i, *tab = cell_tables;
    s16 r;

    const LevelBg *lb = &level_bgs[game.level];

    astral_hidden = FALSE;
    memset(star_clear, 1, sizeof star_clear);
    memset(back_shadow, 0, sizeof back_shadow);
    PAL_setColors(0, lb->palette, 32, DMA);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    for (i = 0; i < lb->pool_need; i++) free_slots[i] = TILE_USER_INDEX + lb->pool_need - 1 - i;
    free_count = lb->pool_need;

    for (i = 0; i < BG_LAYER_COUNT; i++) {
        Layer *l = &layers[i];
        l->def = &lb->layers[i];
        l->plane = planes[i];
        l->slot = tab;
        l->ref = tab + l->def->cells;
        tab += 2 * l->def->cells;
        memset(l->ref, 0, l->def->cells * 2);
        l->rows = l->def->mrows * 7;
        layer_target(i, &l->vy_cur, &l->x_cur);
        l->vy_prev = l->vy = l->vy_cur;
        l->x_prev = l->x_cur;
        rendered_x[i] = l->x_cur;
        l->top = want_top(l);
        for (r = l->top; r < l->top + BG_WIN_ROWS && r < (s16)l->rows; r++) {
            load_cols(l, r, 0, l->def->mcols);
            write_row(l, r, DMA_QUEUE_COPY);
            dmaq_flush();
            DMA_flushQueue();
        }
        l->loading = -1;
        VDP_setHorizontalScroll(l->plane, l->x_cur);
        VDP_setVerticalScroll(l->plane, l->vy);
    }
    last_tick = game.frame;
}

/* prev + (cur - prev) * frac / 12 (21/256 ~ 1/12), one muls.w */
static s16 lerp(s16 prev, s16 cur, u16 frac)
{
    return prev + (s16)(((s32)(s16)(cur - prev) * (s16)(frac * 21)) >> 8);
}

/* Astral Zone swaps Plane B's map in one DMA. A WRAM mirror follows the
 * normal row stream, including while hidden, so restoration requires no
 * tile decoding/refcount changes and does not clobber partially loaded rows. */
static void astral_background(bool hidden)
{
    static const u16 empty[PLANE_ROWS * 64] = {0};
    if (hidden == astral_hidden) return;
    astral_hidden = hidden;
    VDP_setTileMapData(VDP_getBGBAddress(), hidden ? empty : back_shadow[0],
                       0, PLANE_ROWS * 64, 2, DMA_QUEUE);
}

void bg_star_masks(StarMask masks[2])
{
    u16 i;
    for (i = 0; i < BG_LAYER_COUNT; i++) {
        masks[i].cells = i == 0 && astral_hidden ? NULL : star_clear[i][0];
        masks[i].x = SX_OFF - rendered_x[i];
        masks[i].y = SY_OFF + layers[i].vy;
        masks[i].columns = layers[i].def->mcols * 3;
    }
}

void bg_update(u16 frac, bool light)
{
    u16 i;
    bool worked = FALSE;
    bool ticked = game.frame != last_tick;

    last_tick = game.frame;
    astral_background(starfield.draw_astral);
    for (i = 0; i < BG_LAYER_COUNT; i++) {
        Layer *l = &layers[i];
        if (ticked) {
            l->vy_prev = l->vy_cur;
            l->x_prev = l->x_cur;
            layer_target(i, &l->vy_cur, &l->x_cur);
        }
        l->vy = lerp(l->vy_prev, l->vy_cur, frac);
        if (l->loading < 0 && want_top(l) < l->top) {
            l->loading = l->top - 1;
            l->step = 0;
        }
        /* light frames (a game tick ran) skip the streaming work unless the
         * row is already due: the margin row is gone */
        if (!worked && l->loading >= 0 && (!light || want_top(l) < l->loading)) {
            u16 half = l->def->mcols / 2;
            worked = TRUE;
            if (l->step == 0) {
                if (l->top - 1 + BG_WIN_ROWS < (s16)l->rows) release_row(l, l->top - 1 + BG_WIN_ROWS);
            } else if (l->step == 1) {
                load_cols(l, l->loading, 0, half);
            } else {
                load_cols(l, l->loading, half, l->def->mcols);
                write_row(l, l->loading, DMA_QUEUE_COPY);
                l->top = l->loading;
                l->loading = -1;
            }
            l->step++;
        }
        rendered_x[i] = lerp(l->x_prev, l->x_cur, frac);
        VDP_setHorizontalScroll(l->plane, rendered_x[i]);
        VDP_setVerticalScroll(l->plane, l->vy);
    }
}

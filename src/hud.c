/* hud.c — in-game HUD drawn on the window plane: the right panel (x >=
 * 272, 6 columns) and the bottom bar (y >= 208, 2 rows) around the PC
 * playfield.  Tyrian's TINY_FONT (one cell per character) replaces SGDK's
 * font in VRAM.  Window tiles are high priority, over the sprites.
 * Values are redrawn only when they change (a few tilemap writes). */
#include "hud.h"
#include "game/game.h"
#include "game/player.h"
#include "gen_data/fonts.h"
#include "gen_data/script.h"
#include "game/enemy.h"

#define COL0 34                  /* right panel: columns 34..39 */
#define ROW_BOTTOM 26
#define ATTR TILE_ATTR_FULL(PAL2, TRUE, FALSE, FALSE, 0)

static u16 bar_tile, solid_tile;
static u32 shown_cash;
static u16 shown_option[2];
static u8 shown_armor, shown_shield, shown_power, shown_front, shown_rear, dirty;
static u32 text_until; /* game tick deadline, independent of redraw rate */
static u16 shown_boss;           /* armor shown on the boss bar + 1, 0 = hidden */

static void put_text(u16 x, u16 y, const char *s, u16 w)
{
    u16 buf[40], n = 0;
    while (n < w) {
        char c;
        while (*s == '~') s++;     /* highlight toggles (data cube markup) */
        c = *s ? *s++ : ' ';
        if ((u8)c < 32 || (u8)c > 127) c = ' ';
        buf[n++] = ATTR | (TILE_FONT_INDEX + c - 32);
    }
    VDP_setTileMapDataRow(WINDOW, buf, y, x, w, DMA_QUEUE_COPY);
}

/* horizontal bar over the 6 panel columns: value of max, color 0..2 */
static void put_bar(u16 y, u16 value, u16 max, u16 color)
{
    u16 buf[6], i, px = max ? (u16)((u32)value * 48 / max) : 0;
    for (i = 0; i < 6; i++) {
        u16 f = px > 8 ? 8 : px;
        buf[i] = ATTR | (bar_tile + color * 9 + f);
        px -= f;
    }
    VDP_setTileMapDataRow(WINDOW, buf, y, COL0, 6, DMA_QUEUE_COPY);
}

static void num(char *out, u32 v, u16 w)
{
    u16 i;
    for (i = w; i > 0; i--) {
        out[i - 1] = v || i == w ? '0' + (char)(v % 10) : ' ';
        v /= 10;
    }
    out[w] = 0;
}

void hud_init(u16 first_tile, const char *level_name)
{
    static const u32 solid[8] = { 0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE,
                                  0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE };
    u16 y, row[40];

    bar_tile = first_tile;
    solid_tile = first_tile + 27;
    VDP_loadTileData(hud_font_tiles, TILE_FONT_INDEX, 96, DMA);
    VDP_loadTileData(hud_bar_tiles, bar_tile, 27, DMA);
    VDP_loadTileData(solid, solid_tile, 1, DMA);
    /* opaque black frame: transparent window tiles would show plane B */
    for (y = 0; y < 40; y++) row[y] = ATTR | solid_tile;
    for (y = 0; y < 28; y++) {
        if (y >= ROW_BOTTOM)
            VDP_setTileMapDataRow(WINDOW, row, y, 0, 40, DMA);
        else
            VDP_setTileMapDataRow(WINDOW, row, y, COL0, 6, DMA);
    }
    put_text(COL0, 1, "CASH", 6);
    put_text(COL0, 4, "ARMOR", 6);
    put_text(COL0, 7, "SHIELD", 6);
    put_text(COL0, 10, "POWER", 6);
    put_text(COL0, 13, "FRONT", 6);
    put_text(COL0, 16, "REAR", 6);
    put_text(COL0, 19, "OPTION", 6);
    put_text(COL0, 23, level_name, 6);
    put_text(COL0, 24, strlen(level_name) > 6 ? level_name + 6 : "", 6);
    dirty = 1;
    text_until = 0;
    shown_boss = 0;
    hud_update();
}

void hud_message(const char *text)
{
    put_text(1, ROW_BOTTOM, text, 32);
}

/* draw_boss_bar: the most damaged enemy of the linked group, bottom row */
static void boss_bar(void)
{
    u16 armor = 256, i, buf[24];
    if (game.boss_link[0]) {
        for (i = 0; i < ENEMY_MAX; i++)
            if (enemy_avail[i] != EA_FREE && enemies[i].linknum == game.boss_link[0] && enemies[i].armorleft < armor)
                armor = enemies[i].armorleft;
        if (armor > 255 || armor == 0) game.boss_link[0] = 0;
    }
    if (!game.boss_link[0]) {
        if (shown_boss) {
            shown_boss = 0;
            put_text(1, ROW_BOTTOM + 1, "", 32);
        }
        return;
    }
    if (armor == 255) armor = 254;
    if (armor + 1 == shown_boss) return;
    shown_boss = armor + 1;
    {   /* "BOSS" + 16 cells = 128 px for 254 armor */
        u16 px = armor / 2;
        put_text(1, ROW_BOTTOM + 1, "BOSS", 4);
        for (i = 0; i < 16; i++) {
            u16 f = px > 8 ? 8 : px;
            buf[i] = ATTR | (bar_tile + 0 * 9 + f);
            px -= f;
        }
        VDP_setTileMapDataRow(WINDOW, buf, ROW_BOTTOM + 1, 6, 16, DMA_QUEUE_COPY);
    }
}

void hud_update(void)
{
    const Player *p = &players[0];
    char buf[8];
    u8 power = (u8)(game.power / 10);

    if (dirty || p->cash != shown_cash) {
        shown_cash = p->cash;
        num(buf, p->cash > 999999 ? 999999 : p->cash, 6);
        put_text(COL0, 2, buf, 6);
    }
    if (dirty || p->armor != shown_armor) {
        shown_armor = p->armor;
        put_bar(5, p->armor, 28, 0);
    }
    if (dirty || p->shield != shown_shield) {
        shown_shield = p->shield;
        put_bar(8, p->shield, p->shield_max ? p->shield_max : 1, 1);
    }
    if (dirty || power != shown_power) {
        shown_power = power;
        put_bar(11, power, 90, 2);
    }
    if (dirty || p->weapon_power[0] != shown_front) {
        shown_front = p->weapon_power[0];
        strcpy(buf, "LV  ");
        num(buf + 3, shown_front, 2);
        put_text(COL0, 14, p->weapon_id[0] ? buf : "-", 6);
    }
    if (dirty || p->weapon_power[1] != shown_rear) {
        shown_rear = p->weapon_power[1];
        strcpy(buf, "LV  ");
        num(buf + 3, shown_rear, 2);
        put_text(COL0, 17, p->weapon_id[1] ? buf : "-", 6);
    }
    {
        u16 i;
        for (i = 0; i < 2; i++) {
            const Sidekick *s = &p->sidekick[i];
            u16 state = ((u16)p->option_id[i] << 10) | ((u16)s->charge << 7) | s->ammo;
            if (dirty || state != shown_option[i]) {
                shown_option[i] = state;
                if (!p->option_id[i]) strcpy(buf, i ? "R NONE" : "L NONE");
                else {
                    buf[0] = i ? 'R' : 'L';
                    if (s->ammo_max) num(buf + 1, s->ammo, 3);
                    else memcpy(buf + 1, "INF", 3);
                    buf[4] = 'C';
                    buf[5] = '0' + s->charge;
                    buf[6] = 0;
                }
                put_text(COL0, 20 + i, buf, 6);
            }
        }
    }
    if (game.window_text) {   /* JE_drawTextWindow: textErase = 100 game ticks */
        hud_message(window_texts[game.window_text - 1]);
        game.window_text = 0;
        text_until = game.frame + 100;
    } else if (text_until && game.frame >= text_until) {
        text_until = 0;
        hud_message("");
    }
    boss_bar();
    dirty = 0;
}

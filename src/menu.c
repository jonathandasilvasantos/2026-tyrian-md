/* menu.c — text screens: the upgrade shop (with the destination choice of
 * ]G) and simple message screens.  Plane A, Tyrian's SMALL_FONT, palette
 * line 2 (14 black, 15 white). */
#include "menu.h"
#include "game/game.h"
#include "game/player.h"
#include "game/script.h"
#include "game/shop.h"
#include "gen_data/fonts.h"
#include "gen_data/items.h"
#include "gen_data/script.h"
#include "gen_data/sprites.h"
#include "gen_data/title.h"

#define FONT_TILE TILE_USER_INDEX
static u16 held;

void menu_begin(void)
{
    VDP_setEnable(FALSE);
    VDP_setWindowHPos(FALSE, 0);
    VDP_setWindowVPos(FALSE, 0);
    VDP_setHorizontalScroll(BG_A, 0);
    VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_A, 0);
    VDP_setVerticalScroll(BG_B, 0);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    VDP_resetSprites();
    VDP_updateSprites(1, DMA);
    PAL_setColors(32, spr_palettes, 16, DMA);
    PAL_setColor(0, 0);
    VDP_loadTileData(menu_font_tiles, FONT_TILE, 96 * 2, DMA);
    VDP_setEnable(TRUE);
    held = 0xFFFF;   /* ignore buttons held from before */
}

void menu_text(u16 col, u16 row, const char *s, u16 width)
{
    u16 top[40], bot[40], n = 0;
    while (n < width && col + n < 40) {
        char c = *s ? *s++ : ' ';
        u16 t;
        if ((u8)c < 32 || (u8)c > 127) c = ' ';
        t = FONT_TILE + (c - 32) * 2;
        top[n] = TILE_ATTR_FULL(PAL2, FALSE, FALSE, FALSE, t);
        bot[n] = TILE_ATTR_FULL(PAL2, FALSE, FALSE, FALSE, t + 1);
        n++;
    }
    VDP_setTileMapDataRow(BG_A, top, row * 2, col, n, DMA_QUEUE_COPY);
    VDP_setTileMapDataRow(BG_A, bot, row * 2 + 1, col, n, DMA_QUEUE_COPY);
}

u16 menu_pad_edge(void)
{
    u16 pad = JOY_readJoypad(JOY_1), edge = pad & ~held;
    held = pad;
    return edge;
}

void menu_frame(void)
{
    SYS_doVBlankProcess();
}

/* append at most n chars of src (SGDK has no strncat) */
static void cat_n(char *dst, const char *src, u16 n)
{
    dst += strlen(dst);
    while (n-- && *src) *dst++ = *src++;
    *dst = 0;
}

static void num(char *out, u32 v)
{
    char tmp[12];
    u16 n = 0;
    do {
        tmp[n++] = '0' + (char)(v % 10);
        v /= 10;
    } while (v);
    while (n) *out++ = tmp[--n];
    *out = 0;
}

/* item names carry a subtitle after a run of spaces ("USP Talon   Light
 * Fighter"): cut it */
static const char *short_name(const char *s)
{
    static char buf[24];
    u16 n = 0;
    while (s[n] && n < 23 && !(s[n] == ' ' && s[n + 1] == ' ')) {
        buf[n] = s[n];
        n++;
    }
    buf[n] = 0;
    return buf;
}

static const char *item_name_full(u8 cat, u16 item);

static const char *item_name(u8 cat, u16 item)
{
    return short_name(item_name_full(cat, item));
}

static const char *item_name_full(u8 cat, u16 item)
{
    switch (cat) {
    case SHOP_SHIP: return item <= SHIP_NUM ? ship_names[item] : "?";
    case SHOP_FRONT:
    case SHOP_REAR: return item == 0 ? "None" : (item <= PORT_NUM ? port_names[item] : "?");
    case SHOP_GENERATOR: return item <= POWER_NUM ? power_names[item] : "?";
    case SHOP_SHIELD: return item <= SHIELD_NUM ? shield_names[item] : "?";
    }
    return "";
}

static const u8 cats[5] = { SHOP_FRONT, SHOP_REAR, SHOP_GENERATOR, SHOP_SHIELD, SHOP_SHIP };
static const char *const cat_names[5] = { "Front", "Rear", "Power", "Shield", "Ship" };

/* list the items of a category; returns when the player buys or backs out */
static void category_screen(const Shop *sh, u8 cat, const char *title)
{
    u8 n = sh->count[cat], sel = 0, i, redraw = 1;
    u8 power = shop_owned_power(cat);
    u8 weapon = cat == SHOP_FRONT || cat == SHOP_REAR;
    char line[48], buf[12];

    for (i = 0; i < n; i++)
        if (sh->items[cat][i] == shop_owned(cat)) sel = i;
    for (;;) {
        u16 e = menu_pad_edge();
        if (e & BUTTON_UP) { sel = sel ? sel - 1 : n - 1; redraw = 1; }
        if (e & BUTTON_DOWN) { sel = sel + 1 < n ? sel + 1 : 0; redraw = 1; }
        if (weapon && (e & BUTTON_LEFT) && power > 1) { power--; redraw = 1; }
        if (weapon && (e & BUTTON_RIGHT) && power < 11) { power++; redraw = 1; }
        if (e & BUTTON_C) return;
        if (e & (BUTTON_B | BUTTON_START | BUTTON_A)) {
            u16 item = sh->items[cat][sel];
            if (shop_buy(cat, item, weapon ? (item ? power : 1) : 1)) return;
        }
        if (redraw) {
            redraw = 0;
            menu_text(1, 0, title, 38);
            strcpy(line, "Budget ");
            num(line + 7, shop_budget(cat));
            menu_text(1, 1, line, 38);
            for (i = 0; i < 9; i++) {
                if (i < n) {
                    u16 item = sh->items[cat][i];
                    line[0] = i == sel ? '*' : ' ';
                    line[1] = ' ';
                    strncpy(line + 2, item_name(cat, item), 22);
                    line[24] = 0;
                    while (strlen(line) < 25) strcat(line, " ");
                    num(buf, shop_cost(cat, item, weapon ? (item ? power : 1) : 1));
                    strcat(line, buf);
                    menu_text(1, 3 + i, line, 38);
                } else {
                    menu_text(1, 3 + i, "", 38);
                }
            }
            if (weapon) {
                strcpy(line, "Power ");
                num(line + 6, power);
                strcat(line, "   LEFT/RIGHT");
                menu_text(1, 12, line, 38);
            }
            menu_text(1, 13, "B buy   C back", 38);
        }
        menu_frame();
    }
}

u8 shop_screen(u8 shop_index)
{
    const Shop *sh = &shops[shop_index];
    u8 rows[6], nrows = 0, sel, dest = 0, i, redraw = 1;
    char line[48], buf[12];

    for (i = 0; i < 5; i++)
        if (sh->count[cats[i]]) rows[nrows++] = i;
    sel = nrows;   /* start on Launch */
#ifdef AUTOPLAY
    return script.choices ? script.choice_section[0] : 0;
#endif
    menu_begin();
    for (;;) {
        u16 e = menu_pad_edge();
        if (e & BUTTON_UP) { sel = sel ? sel - 1 : nrows; redraw = 1; }
        if (e & BUTTON_DOWN) { sel = sel < nrows ? sel + 1 : 0; redraw = 1; }
        if (sel == nrows && script.choices > 1 && (e & (BUTTON_LEFT | BUTTON_RIGHT))) {
            dest = (u8)((dest + (e & BUTTON_LEFT ? script.choices - 1 : 1)) % script.choices);
            redraw = 1;
        }
        if (e & (BUTTON_B | BUTTON_START | BUTTON_A)) {
            if (sel == nrows) return script.choices ? script.choice_section[dest] : 0;
            category_screen(sh, cats[rows[sel]], cat_names[rows[sel]]);
            menu_begin();
            redraw = 1;
        }
        if (redraw) {
            redraw = 0;
            menu_text(1, 0, "UPGRADE SHIP", 38);
            strcpy(line, "Cash ");
            num(line + 5, players[0].cash);
            menu_text(1, 1, line, 38);
            for (i = 0; i < nrows; i++) {
                u8 c = cats[rows[i]];
                line[0] = i == sel ? '*' : ' ';
                line[1] = ' ';
                strcpy(line + 2, cat_names[rows[i]]);
                while (strlen(line) < 10) strcat(line, " ");
                cat_n(line, item_name(c, shop_owned(c)), 20);
                if ((c == SHOP_FRONT || c == SHOP_REAR) && shop_owned(c)) {
                    strcat(line, " ");
                    num(buf, shop_owned_power(c));
                    strcat(line, buf);
                }
                menu_text(1, 3 + i, line, 38);
            }
            line[0] = sel == nrows ? '*' : ' ';
            strcpy(line + 1, " Launch: ");
            strcat(line, script.choices ? planet_names[script.choice_planet[dest] <= PLANET_COUNT ? script.choice_planet[dest] : 0] : "");
            if (script.choices > 1) strcat(line, "  (L/R)");
            menu_text(1, 4 + nrows, line, 38);
        }
        menu_frame();
    }
}

void message_screen(const char *line1, const char *line2)
{
    menu_begin();
    menu_text(2, 5, line1, 36);
    menu_text(2, 7, line2, 36);
    menu_text(2, 10, "Press START", 36);
    while (!(menu_pad_edge() & (BUTTON_START | BUTTON_B | BUTTON_A))) menu_frame();
}

/* centered text, 40 columns */
static void menu_center(u16 row, const char *s)
{
    u16 len = strlen(s);
    menu_text(len < 40 ? (40 - len) / 2 : 0, row, s, len);
}

void title_screen(void)
{
    u16 i, blink = 0, row[40];

    menu_begin();
    VDP_setEnable(FALSE);
    PAL_setColors(0, title_palette, 32, CPU);
    PAL_setColors(48, title_palette + 48, 16, CPU);
    VDP_loadTileData(title_tiles, FONT_TILE + 192, TITLE_TILES, DMA);
    for (i = 0; i < 28; i++) {
        u16 x;
        for (x = 0; x < 40; x++) row[x] = title_map[i * 40 + x] + FONT_TILE + 192;
        VDP_setTileMapDataRow(BG_B, row, i, 0, 40, DMA);
    }
    VDP_setEnable(TRUE);
    /* credits on black bands (the font's opaque background) under the art */
    menu_center(9, " Mega Drive version by Jonathan Santos ");
    menu_center(10, " Tyrian by Jason Emery and Daniel Cook ");
    menu_center(11, " Music by A. Brandon and A. Molnar ");
    menu_center(12, " Copyright 1995 Epic MegaGames ");
#ifdef AUTOPLAY
    return;
#endif
    for (;;) {
        if ((blink++ & 31) == 0) menu_center(13, blink & 32 ? "            " : " PRESS START ");
        if (menu_pad_edge() & (BUTTON_START | BUTTON_A | BUTTON_B)) break;
        menu_frame();
    }
    menu_center(13, "            ");
}

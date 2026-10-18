/* main.c — Genesis entry point and game flow (hardware side of agents.md §5):
 * episode script -> shops and levels; a level runs until it ends or the
 * player dies (then it is replayed with the items it started with). */
#include <genesis.h>
#include "game/game.h"
#include "game/player.h"
#include "game/script.h"
#include "gen_data/script.h"
#include "bg.h"
#include "sprites.h"
#include "dmaq.h"
#include "hud.h"
#include "menu.h"
#include "sound.h"

/* VRAM budget (agents.md §9): background pool, sprite caches and the HUD
 * tiles must end below the font (TILE_FONT_INDEX, 96 tiles below the
 * tilemaps at 0xC000), which holds the HUD font. */
#define HUD_VRAM_TILES 28
_Static_assert(BG_VRAM_END + SPRITES_VRAM_TILES + HUD_VRAM_TILES <= 0xC000 / 32 - 96, "VRAM over budget");

/* State hash after the latest tick; tools/bench68k reads it by symbol to
 * compare the ROM with the host build on the same replay. */
volatile u32 frame_hash;
/* set by tools/bench68k: game_hash() costs ~5k cycles, only for parity */
volatile u8 hash_enabled;
/* set by tools/bench68k (B68K_LEVEL): play this level (1-based) directly */
volatile u8 debug_level;

/* game_tick: one simulation step.  Kept as a real function (noinline) so
 * tools/bench68k can count ticks and feed replay pads by its address. */
__attribute__((noinline)) void game_tick(u16 pad)
{
    game_step(pad);
}

static void level_screen_setup(const char *name)
{
    VDP_setEnable(FALSE);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    /* HUD frame: window plane on the right (x >= 272) and bottom (y >= 208) */
    VDP_setWindowHPos(TRUE, 17);
    VDP_setWindowVPos(TRUE, 26);
    dmaq_init();
    bg_init();
    sprites_init(BG_VRAM_END);
    hud_init(BG_VRAM_END + SPRITES_VRAM_TILES, name);
    VDP_setEnable(TRUE);
}

/* Play one level; returns 1 if the player died. */
static u8 play_level(const FlowStep *st)
{
    u16 acc = 0;

    game_start_level(st->level);
    level_screen_setup(level_names[st->name]);
    sound_music(st->song);
    while (TRUE) {
        bool ticked = FALSE;
        for (acc += TICK_NUM; acc >= TICK_DEN; acc -= TICK_DEN) {
            ticked = TRUE;
#ifdef AUTOPLAY
            /* make rom-demo: fire and sweep, for emulator captures */
            game_tick(BUTTON_B | ((game.frame / 70) & 1 ? BUTTON_RIGHT : BUTTON_LEFT)
                      | ((game.frame / 45) & 1 ? BUTTON_UP : 0));
#else
            game_tick(JOY_readJoypad(JOY_1));
#endif
            if (hash_enabled) frame_hash = game_hash();
        }
        /* tick frames carry the game logic: keep the rest for the others */
        bg_update(acc, ticked);
        sprites_update(acc, ticked);
        if (!ticked) {
            sound_flush();
            hud_update();
        }
        SYS_doVBlankProcess();
        if (game.really_end_level) return 0;
        if (!players[0].is_alive && players[0].exploding_ticks == 0) return 1;
    }
}

int main(bool hard)
{
    VDP_setScreenWidth320();
    sound_init();
    game_init();          /* new game: items, RNG */
    script_start();
    if (debug_level) {    /* benchmark entry: one level, replayed forever */
        FlowStep st = { FLOW_LEVEL, (u8)(debug_level - 1), 0, 0, 0, 0, 0 };
        while (TRUE) {
            game_init();
            play_level(&st);
        }
    }

    while (TRUE) {
        FlowStep st = script_run();
        switch (st.kind) {
        case FLOW_SHOP:
            sound_music(script.shop_song);
            script_goto(shop_screen(st.shop));
            break;
        case FLOW_LEVEL: {
            Player saved = players[0];   /* death replays the level with these items */
            u8 died = play_level(&st);
            if (died) players[0] = saved;
            script_level_done(st.next_section, died);
            break;
        }
        case FLOW_END:
            message_screen("EPISODE COMPLETE", level_names[0]);
            game_init();
            script_start();
            break;
        }
    }
    return 0;
}

/* game.h — portable Tyrian simulation (no SGDK headers, see agents.md §5).
 *
 * Compiled for the ROM (src/main.c) and for the host (platform/host/).
 * Same pad inputs => same state on both, checked through game_hash().
 */
#ifndef GAME_H
#define GAME_H

#include "types.h"

/* Pad bits, same layout as SGDK's BUTTON_* (JOY_readJoypad). */
#define PAD_UP    0x0001
#define PAD_DOWN  0x0002
#define PAD_LEFT  0x0004
#define PAD_RIGHT 0x0008
#define PAD_A     0x0040
#define PAD_B     0x0010
#define PAD_C     0x0020
#define PAD_START 0x0080
#define PAD_Z     0x0100
#define PAD_Y     0x0200
#define PAD_X     0x0400
/* Hardware layer marks 3-button input: A triggers both limited sidekicks. */
#define PAD_THREE_BUTTON 0x1000

/* Original game rate: PIT 0x4300 (69.57 Hz) / frameCountMax 2 = 34.78 Hz.
 * The Genesis runs TICK_NUM ticks every TICK_DEN 60 Hz frames (35 Hz,
 * +0.6%); PAL uses 7 ticks per 10 display frames. See timing.h and §8. */
#define TICK_NUM 7
#define TICK_DEN 12

/* sound ids (sndmast.h) */
#define S_ENEMY_HIT 3
#define S_EXPLOSION_4 4
#define S_EXPLOSION_8 8
#define S_EXPLOSION_9 9
#define S_EXPLOSION_11 11
#define S_WARNING 17
#define S_CLICK 24
#define S_ITEM 18
#define S_HULL_HIT 19
#define S_EXPLOSION_22 22
#define S_SHIELD_HIT 27
#define S_POWERUP 29
#define V_GOOD_LUCK 33
#define V_DATA_CUBE 37

#define DIFFICULTY_EASY 1
#define DIFFICULTY_NORMAL 2
#define DIFFICULTY_HARD 3
#define DIFFICULTY_IMPOSSIBLE 4
#define DIFFICULTY_INSANITY 5
#define DIFFICULTY_SUICIDE 6
#define DIFFICULTY_MANIACAL 7
#define DIFFICULTY_ZINGLON 8
#define DIFFICULTY_NORTANEOUS 9
#define DIFFICULTY_10 10

/* One background layer's scroll state, as tyrian2.c/backgrnd.c keep it. */
typedef struct {
    int16_t ptr;          /* mapYPos as an index into the layer map (row * width - 1) */
    int16_t wrap, wrap_to;/* BKwrap / BKwrapto, same units */
    uint8_t back_pos;     /* 0..27 px into the current row */
    uint8_t delay, delay_max;
    int16_t wrap_rows;    /* rows skipped by wraps/jumps: virtual row = row + wrap_rows */
} LayerScroll;

typedef struct {
    uint8_t initial_episode, repeated; /* story campaign wrap state */
    uint8_t level;            /* levels[] index = lvlFileNum - 1 */
    uint32_t frame;           /* game ticks so far */
    uint16_t cur_loc;         /* curLoc: scroll position counter driving events */
    uint16_t event_loc;       /* eventLoc, 1-based */
    uint8_t back_move[3];     /* backMove..3, px per tick */
    uint8_t explode_move;
    LayerScroll layer[3];
    int16_t map_x_ofs[3];     /* mapXOfs, mapX2Ofs, mapX3Ofs (parallax from player X) */
    int16_t old_map_x_ofs, old_map_x3_ofs;
    uint16_t power;           /* generator energy, 0..900 */
    uint8_t shield_wait;
    uint16_t shield_t;
    int8_t difficulty, initial_difficulty, base_difficulty;
    uint8_t enemies_active, level_enemy_frequency, force_events;
    uint8_t stop_backgrounds, stop_background_num;
    uint8_t background2over, background3over, background3x1, background3x1b;
    uint8_t top_enemy_over, sky_enemy_over_all, small_enemy_adjust;
    uint8_t enemy_continual_damage, damage_rate, random_explosions;
    uint8_t enemy_on_screen, enemy_still_exploding;
    uint16_t super_enemy_254_jump;
    uint8_t global_flags[10];
    uint8_t new_pl[10];
    uint16_t return_loc;
    uint8_t return_active;
    uint8_t end_level, ready_to_end_level, really_end_level, level_end;
    int8_t level_end_warp;
    uint16_t armor_ship_delay;
    uint8_t warning_sound_delay;
    uint16_t enemy_killed, total_enemy, cube_max;
    uint8_t custom_armor;     /* events 49-52 write enemyDat[0] (ROM here) */
    uint16_t custom_egr;
    uint8_t boss_link[2];
    uint8_t level_timer, timer_phase100, timer_phase20;
    uint8_t music_volume, music_fade; /* PC tempVolume/musicFade, fixed maximum 255 */
    uint8_t bonus_section, bonus_display, music_track; /* pickup destination, 150-tick notice, 1-based song */
    uint8_t sound_queue[8];
    uint8_t window_text;      /* event 16 text 1..9 or bonus notice 10, 0 = none (renderer clears it) */   /* soundQueue: sample id per channel, consumed each tick */
    uint16_t level_timer_countdown, level_timer_jump_to;
} GameState;

extern GameState game;

void game_init(void);                   /* new game: player items + first level */
void game_start_level(uint8_t level);   /* keep player items */
void game_step(uint16_t pad);   /* one 35 Hz tick */
uint32_t game_hash(void);

#endif

#include "game.h"
#include "player.h"
#include "shots.h"
#include "enemy.h"
#include "level.h"
#include "rng.h"
#include "gen_data/items.h"
#include "gen_data/level.h"

GameState game;

/* Start level index lv (lvlFileNum - 1) with the current player state. */
void game_start_level(uint8_t lv)
{
    game.level = lv;
    game.frame = 0;
    game.power = 0;
    game.difficulty = game.initial_difficulty;
    game.map_x_ofs[0] = game.map_x_ofs[1] = game.map_x_ofs[2] = 0;
    game.old_map_x_ofs = game.old_map_x3_ofs = 0;
    level_init();
    enemies_init();
    player_start_level();
    shots_init();
    game.shield_wait = 1;
    game.shield_t = shields[players[0].shield_item].tpwr * 20;
    {
        int i;
        for (i = 0; i < 8; i++) game.sound_queue[i] = 0;
    }
    game.sound_queue[3] = V_GOOD_LUCK;
    game.window_text = 0;
}

void game_init(void)
{
    game.initial_difficulty = DIFFICULTY_NORMAL;
    rng_seed(0x2545F491u);
    player_init();
    game_start_level(GAME_FIRST_LEVEL);
}

/* One tick, in tyrian2.c:JE_main order. */
void game_step(uint16_t pad)
{
    Player *p = &players[0];
    uint16_t level_enemy;

    level_wrap();

    if (!game.end_level) {
        /* HUD section: shield recharge from the generator, power bar */
        if (p->is_alive && p->shield < p->shield_max && game.power > game.shield_t) {
            if (--game.shield_wait == 0) {
                game.shield_wait = 15;
                game.power -= game.shield_t;
                p->shield++;
            }
        }
        game.power += generator_power[p->generator];
        if (game.power > 900) game.power = 900;
        game.old_map_x3_ofs = game.map_x_ofs[2];
        game.enemy_on_screen = 0;
    }

    level_run_events();
    level_scroll_1();
    level_scroll_2();     /* layer 2 moves wherever background2over draws it */

    /* ground enemies */
    {
        uint8_t last = game.enemy_on_screen;
        enemies_update(50, game.map_x_ofs[0], game.back_move[0]);
        enemies_update(100, game.map_x_ofs[0], game.back_move[0]);
        if ((game.enemy_on_screen == 0 || game.enemy_on_screen == last) && game.stop_background_num == 1)
            game.stop_background_num = 9;
    }
    if (game.background3over == 2) level_scroll_3();

    /* random enemies */
    if (game.enemies_active && rng_mod(100) > game.level_enemy_frequency) {
        level_enemy = levels[game.level].enemies[rng_mod(levels[game.level].enemy_max)];
        enemy_new(0, level_enemy, 0);
    }

    if (!game.sky_enemy_over_all) {
        uint8_t last = game.enemy_on_screen;
        enemies_update(25, game.map_x_ofs[1], 0);
        if (game.enemy_on_screen == last && game.stop_background_num == 2) game.stop_background_num = 9;
    }
    if (game.background3over == 0) level_scroll_3();
    if (!game.top_enemy_over)
        enemies_update(75, game.background3x1 == 0 ? game.old_map_x3_ofs : game.map_x_ofs[0], game.back_move[2]);

    shots_move();
    p->last_x_shot_move = p->x;
    p->last_y_shot_move = p->y;

    if (p->is_alive && !game.end_level) player_collide();
    player_tick(pad);

    if (!game.end_level) enemy_shots_update();
    if (game.background3over == 1) level_scroll_3();
    if (game.top_enemy_over)
        enemies_update(75, game.background3x1 == 0 ? game.old_map_x3_ofs : game.old_map_x_ofs, game.back_move[2]);
    if (game.sky_enemy_over_all) {
        uint8_t last = game.enemy_on_screen;
        enemies_update(25, game.map_x_ofs[1], 0);
        if (game.enemy_on_screen == last && game.stop_background_num == 2) game.stop_background_num = 9;
    }

    explosions_update();

    /* low-armor rescue ship */
    if (p->is_alive && p->armor < 6) {
        if (game.armor_ship_delay > 0) {
            game.armor_ship_delay--;
        } else {
            int b = enemy_new(50, 560, 0);
            if (b) {
                enemies[b - 1].enemydie = 560 + rng_mod(3) + 1;
                enemies[b - 1].eyc -= game.back_move[2];
                enemies[b - 1].armorleft = 4;
            }
            game.armor_ship_delay = 500;
        }
    }
    if (game.return_active && game.enemy_on_screen == 0) {
        level_event_jump(65535);
        game.return_active = 0;
    }
    if (game.level_timer && game.level_timer_countdown > 0) {
        if (--game.level_timer_countdown == 0) level_event_jump(game.level_timer_jump_to);
    }
    if (game.random_explosions && rng_mod(10) == 1)
        explosion_setup_large(0, 20, rng_mod(280), rng_mod(180));

    /* backgrounds restart once the targeted enemies are gone; level end
     * once readyToEndLevel and the screen is clear */
    if (game.stop_background_num == 9 && game.back_move[0] == 0 && !game.enemy_still_exploding) {
        game.back_move[0] = 1;
        game.back_move[1] = 2;
        game.back_move[2] = 3;
        game.explode_move = 2;
        game.stop_background_num = 0;
        game.stop_backgrounds = 0;
    }
    if (!game.end_level && game.enemy_on_screen == 0) {
        if (game.ready_to_end_level && !game.enemy_still_exploding) {
            game.ready_to_end_level = 0;
            game.end_level = 1;
            game.level_end = 40;
        }
        if (game.stop_backgrounds) {
            game.stop_backgrounds = 0;
            game.back_move[0] = 1;
            game.back_move[1] = 2;
            game.back_move[2] = 3;
            game.explode_move = 2;
        }
    }

    game.frame++;
}

/* Rotate-xor-add over the fields one by one, so host and 68000 agree
 * regardless of struct padding and endianness.  No multiply: the 68000 has
 * no 32-bit one and FNV cost ~4k cycles per frame through __mulsi3. */
static uint32_t mix(uint32_t h, uint32_t v)
{
    h = (h << 5 | h >> 27) ^ v;
    return h + 0x9E3779B9u;
}

uint32_t game_hash(void)
{
    const Player *p = &players[0];
    uint32_t h = 2166136261u;
    int i;

    h = mix(h, game.frame);
    h = mix(h, game.cur_loc | (uint32_t)game.event_loc << 16);
    h = mix(h, game.power | (uint32_t)p->cash << 16);
    h = mix(h, rng_state);
    h = mix(h, (uint16_t)p->x | (uint32_t)(uint16_t)p->y << 16);
    h = mix(h, (uint16_t)p->x_velocity | (uint32_t)(uint16_t)p->y_velocity << 16);
    h = mix(h, p->weapon_id[0] | (uint32_t)p->weapon_id[1] << 16);
    h = mix(h, p->weapon_power[0] | p->weapon_power[1] << 8 | (uint32_t)p->weapon_mode << 16);
    h = mix(h, p->armor | p->shield << 8 | (uint32_t)p->is_alive << 16);
    for (i = 0; i < PLAYER_SHOT_MAX; i++) {
        const PlayerShot *s = &player_shots[i];
        if (!s->avail) continue;
        h = mix(h, i | (uint32_t)s->avail << 8);
        h = mix(h, (uint16_t)s->x | (uint32_t)(uint16_t)s->y << 16);
    }
    for (i = 0; i < ENEMY_MAX; i++) {
        const Enemy *e = &enemies[i];
        if (enemy_avail[i] == EA_FREE) continue;
        h = mix(h, i | (uint32_t)e->armorleft << 8);
        h = mix(h, (uint16_t)e->ex | (uint32_t)(uint16_t)e->ey << 16);
    }
    for (i = 0; i < ENEMY_SHOT_MAX; i++) {
        const EnemyShot *s = &enemy_shots[i];
        if (s->avail) continue;
        h = mix(h, (uint16_t)s->sx | (uint32_t)(uint16_t)s->sy << 16);
    }
    return h;
}

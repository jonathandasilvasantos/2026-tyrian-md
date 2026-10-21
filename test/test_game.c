/* test_game.c — host tests for src/game (tier 0). */
#include <stdio.h>
#include "game/game.h"
#include "game/player.h"
#include "game/shots.h"
#include "game/enemy.h"
#include "game/script.h"
#include "game/shop.h"
#include "game/level.h"
#include "game/episode.h"
#include "game/rng.h"
#include "game/timing.h"
#include "game/sidekick.h"
#include "gen_data/items.h"
#include "gen_data/episodes.h"
#include "gen_data/script.h"
#include "gen_data/level.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static uint8_t test_episode = 1;
static void test_init(void)
{
    game_new(test_episode, DIFFICULTY_NORMAL);
}

static uint32_t run(uint16_t pad, unsigned frames)
{
    unsigned i;
    test_init();
    for (i = 0; i < frames; i++) game_step(pad);
    return game_hash();
}

static int live_shots(void)
{
    int i, n = 0;
    for (i = 0; i < PLAYER_SHOT_MAX; i++) n += player_shots[i].avail != 0;
    return n;
}

static void test_story_start(void)
{
    static const uint16_t cash[4] = {10000, 15000, 20000, 30000};
    test_init();
    CHECK(game.level == LEVEL_FIRST);
    CHECK(players[0].cash == cash[LEVEL_EPISODE - 1]);
    players[0].cash += 1234;
    game_start_level(LEVEL_FIRST);
    CHECK(players[0].cash == cash[LEVEL_EPISODE - 1] + 1234u);
    test_init();
    CHECK(players[0].cash == cash[LEVEL_EPISODE - 1]);
}

static void test_deterministic(void)
{
    CHECK(run(PAD_LEFT | PAD_UP | PAD_B, 300) == run(PAD_LEFT | PAD_UP | PAD_B, 300));
    CHECK(run(PAD_LEFT, 300) != run(PAD_RIGHT, 300));
}

static void test_ship_stays_in_pc_bounds(void)
{
    unsigned i;
    /* Exercise movement without level enemies killing the test player. */
    test_init();
    for (i = 0; i < 400; i++) player_tick(PAD_LEFT | PAD_UP);
    CHECK(players[0].x == 40 && players[0].y == 10);
    test_init();
    for (i = 0; i < 400; i++) player_tick(PAD_RIGHT | PAD_DOWN);
    CHECK(players[0].x == 256 && players[0].y == 160);
}

static void test_velocity_is_capped(void)
{
    run(PAD_RIGHT, 20);
    CHECK(players[0].x_velocity == 4);
}

static void test_pulse_cannon_fires_and_shots_leave(void)
{
    run(PAD_B, 40);
    CHECK(live_shots() > 0);
    /* Pulse Cannon: 10 px/tick up from y 160 -> off the top within ~20 ticks */
    { int i; for (i = 0; i < 40; i++) game_step(0); }
    CHECK(live_shots() == 0);
}

static void test_generator_limits_fire(void)
{
    /* MR-12 gives 14/tick, Pulse Cannon costs 30 per shot */
    run(0, 1);
    CHECK(game.power == 14);
}

static void test_level_spawns_and_kills_enemies(void)
{
    unsigned i;
    test_init();
    for (i = 0; i < 1500; i++) game_step(PAD_B | ((i / 70) & 1 ? PAD_RIGHT : PAD_LEFT));
    CHECK(game.total_enemy > 0);
    CHECK(game.enemy_killed > 0);
    /* Enemy kills need not yield a collected cash item on this replay. */
    CHECK(game.event_loc > 1);
}

static void test_events_follow_cur_loc(void)
{
    uint16_t next_event;
    test_init();
    level_run_events();
    next_event = game.event_loc;
    CHECK(next_event > 1);  /* time-zero setup has run */
    CHECK(levels[game.level].events[next_event - 1].time > game.cur_loc);
    game.cur_loc = levels[game.level].events[next_event - 1].time;
    level_run_events();
    CHECK(game.event_loc > next_event);
}

/* Walk the episode script winning every level and taking the first
 * destination: it must reach the end, through valid levels. */
static void test_script_walk(void)
{
    int steps = 0, levels_played = 0;
    script_start();
    for (; steps < 200; steps++) {
        FlowStep st = script_run();
        if (st.kind == FLOW_END) break;
        if (st.kind == FLOW_SHOP) {
            CHECK(st.shop < SHOP_COUNT);
            script_goto(script.choices ? script.choice_section[0] : 0);
        } else {
            CHECK(st.level < LEVEL_COUNT);
            levels_played++;
            script_level_done(st.next_section, 0);
        }
    }
    CHECK(steps < 200);
    CHECK(levels_played >= 5);
}

static void test_shop_trade_in(void)
{
    uint32_t c1 = shop_cost(SHOP_FRONT, 1, 1), c3 = shop_cost(SHOP_FRONT, 1, 3);
    test_init();
    players[0].cash = 0;
    /* full trade-in value: selling Pulse Cannon Lv1 buys it back */
    CHECK(shop_buy(SHOP_FRONT, 1, 1) && players[0].cash == 0);
    CHECK(c3 == c1 + c1 * 1 + c1 * 3);      /* + cost*1, + cost*(1+2) */
    CHECK(!shop_buy(SHOP_FRONT, 1, 3));     /* cannot afford */
    players[0].cash = c3 - c1;
    CHECK(shop_buy(SHOP_FRONT, 1, 3) && players[0].cash == 0 && players[0].weapon_power[0] == 3);
}

static void test_episode_transition(void)
{
    unsigned ep;
    game_new(1, DIFFICULTY_NORMAL);
    players[0].cash = 123456;
    players[0].ship = 3;
    players[0].weapon_id[0] = 17;
    players[0].weapon_power[0] = 8;
    players[0].option_id[0] = 3;
    players[0].option_id[1] = 30;
    for (ep = 2; ep <= 4; ep++) {
        uint32_t saved_rng = rng_state;
        game_next_episode();
        CHECK(LEVEL_EPISODE == ep);
        CHECK(players[0].cash == 123456 && players[0].ship == 3);
        CHECK(players[0].weapon_id[0] == 17 && players[0].weapon_power[0] == 8);
        CHECK(players[0].option_id[0] == 3 && players[0].option_id[1] == 30);
        CHECK(rng_state == saved_rng);
        CHECK(script.section == 1 && script.save_section == 1 && script.choices == 0);
    }
    CHECK(enemy_dat[1].armor == 2 && enemy_dat[1].shapebank == 1);
    rng_seed(3);  /* first rng16 is 12: SuperCarrot reward */
    game_next_episode();
    CHECK(LEVEL_EPISODE == 1 && game.repeated);
    CHECK(players[0].cash == 123456);
    CHECK(players[0].ship == 2 && players[0].weapon_id[0] == 23 && players[0].weapon_id[1] == 24);
    CHECK(players[0].weapon_power[0] == 1 && players[0].weapon_power[1] == 1);
    CHECK(enemy_dat[1].armor == 254 && enemy_dat[1].shapebank == 2);
}

static void test_pal_and_ntsc_timing(void)
{
    uint32_t hashes[2];
    unsigned pal;
    for (pal = 0; pal < 2; pal++) {
        TickClock clock;
        unsigned frame, ticks = 0, frames = pal ? 250 : 300;
        game_new(1, DIFFICULTY_NORMAL);
        tick_clock_init(&clock, pal);
        for (frame = 0; frame < frames; frame++) {
            if (tick_clock_step(&clock)) {
                game_step(PAD_B | (ticks / 30 & 1 ? PAD_LEFT : PAD_RIGHT));
                ticks++;
            }
            CHECK(tick_clock_fraction(&clock) < TICK_DEN);
        }
        CHECK(ticks == 175); /* five seconds, 35 Hz in either region */
        CHECK(clock.accumulator == 0);
        hashes[pal] = game_hash();
    }
    CHECK(hashes[0] == hashes[1]);
}

static void equip_sidekicks(uint8_t left, uint8_t right)
{
    test_init();
    players[0].option_id[0] = left;
    players[0].option_id[1] = right;
    game_start_level(LEVEL_FIRST);
    game.power = 900;
}

static void test_sidekick_shop(void)
{
    uint32_t budget;
    test_init();
    players[0].cash = 100000;
    CHECK(shop_buy(SHOP_LEFT_OPTION, 3, 1));
    CHECK(players[0].option_id[0] == 3);
    budget = players[0].cash + option_cost[3];
    CHECK(shop_buy(SHOP_LEFT_OPTION, 0, 1));
    CHECK(players[0].cash == budget && players[0].option_id[0] == 0);
    CHECK(shop_buy(SHOP_RIGHT_OPTION, 6, 1));
    CHECK(players[0].option_id[1] == 6);
    budget = players[0].cash;
    CHECK(!shop_buy(SHOP_RIGHT_OPTION, OPTION_NUM + 1, 1));
    CHECK(players[0].cash == budget && players[0].option_id[1] == 6);
}

static void test_sidekick_charge_and_ammo(void)
{
    unsigned i;
    equip_sidekicks(3, 6); /* charge cannon and five-ammo MegaMissile */
    for (i = 0; i < 40; i++) sidekicks_animate();
    CHECK(players[0].sidekick[0].charge == 2);
    sidekicks_tick(0); /* initial shot cooldown */
    sidekicks_tick(PAD_B);
    CHECK(players[0].sidekick[0].charge == 0);
    CHECK(players[0].sidekick[1].ammo == 5); /* main fire cannot use finite ammo */
    CHECK(live_shots() > 0);
    sidekicks_tick(PAD_Z);
    CHECK(players[0].sidekick[1].ammo == 4);
    players[0].shot_repeat[SHOT_RIGHT_SIDEKICK] = 0;
    players[0].sidekick[1].refill_ticks = 0;
    sidekicks_tick(0);
    CHECK(players[0].sidekick[1].ammo == 5);
    CHECK(players[0].sidekick[1].refill_ticks == 400); /* (105 - 5) * 4 */
    equip_sidekicks(6, 6);
    sidekicks_tick(0);
    sidekicks_tick(PAD_THREE_BUTTON | PAD_A);
    CHECK(players[0].sidekick[0].ammo == 4 && players[0].sidekick[1].ammo == 4);
}

static void test_sidekick_movement(void)
{
    unsigned i;
    equip_sidekicks(19, 20); /* trailing companion and launchable attachment */
    player_tick(PAD_RIGHT);
    CHECK(players[0].sidekick[0].x == 91 && players[0].sidekick[0].y == 162);
    CHECK(players[0].sidekick[1].x == players[0].x);
    CHECK(players[0].sidekick[1].y == players[0].y - 20);
    player_tick(PAD_Z);
    CHECK(!players[0].attachment_linked && players[0].attachment_move == -20);
    for (i = 0; i < 300 && !players[0].attachment_linked; i++) player_tick(0);
    CHECK(players[0].attachment_linked);
    equip_sidekicks(30, 30);
    player_tick(0);
    CHECK(players[0].sidekick[0].x - players[0].x == 4);
    CHECK(players[0].sidekick[1].x - players[0].x == -4);
    CHECK(players[0].sidekick[0].y - players[0].y == 20);
    CHECK(players[0].sidekick[1].y - players[0].y == -20);
}

static void test_large_projectile_collision(void)
{
    unsigned special;
    for (special = 0; special < 2; special++) {
        PlayerShot *s;
        Enemy *e;
        test_init();
        e = &enemies[0];
        *e = (Enemy){0};
        enemy_avail[0] = EA_ACTIVE;
        e->enemycycle = 1;
        e->ex = 150;
        e->ey = 118;
        e->armorleft = 10;
        s = &player_shots[0];
        *s = (PlayerShot){0};
        s->x = s->y = 100;
        s->gr = special ? 60023 : 1; /* 62x24 Plasma Storm versus a small bullet */
        s->avail = 5;
        s->ani_max = s->player = s->dmg = 1;
        s->trail = 255;
        shots_move();
        CHECK(e->armorleft == (special ? 9 : 10));
        CHECK(s->frame == (special ? 60023 : 1));
    }
}

int main(void)
{
    for (test_episode = 1; test_episode <= EPISODE_COUNT; test_episode++) {
        test_story_start();
        CHECK(game.difficulty == (test_episode == 4 ? DIFFICULTY_EASY : DIFFICULTY_NORMAL));
        test_deterministic();
        test_ship_stays_in_pc_bounds();
        test_velocity_is_capped();
        test_pulse_cannon_fires_and_shots_leave();
        test_generator_limits_fire();
        test_level_spawns_and_kills_enemies();
        test_events_follow_cur_loc();
        test_script_walk();
        test_shop_trade_in();
        test_sidekick_shop();
        test_sidekick_charge_and_ammo();
        test_sidekick_movement();
        test_large_projectile_collision();
    }
    test_episode_transition();
    test_pal_and_ntsc_timing();
    if (failures) return 1;
    puts("   PASS  test_game (four episodes and campaign transitions)");
    return 0;
}

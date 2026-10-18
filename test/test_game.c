/* test_game.c — host tests for src/game (tier 0). */
#include <stdio.h>
#include "game/game.h"
#include "game/player.h"
#include "game/shots.h"
#include "game/enemy.h"
#include "game/script.h"
#include "game/shop.h"
#include "gen_data/script.h"
#include "gen_data/level.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static uint32_t run(uint16_t pad, unsigned frames)
{
    unsigned i;
    game_init();
    for (i = 0; i < frames; i++) game_step(pad);
    return game_hash();
}

static int live_shots(void)
{
    int i, n = 0;
    for (i = 0; i < PLAYER_SHOT_MAX; i++) n += player_shots[i].avail != 0;
    return n;
}

static void test_deterministic(void)
{
    CHECK(run(PAD_LEFT | PAD_UP | PAD_B, 300) == run(PAD_LEFT | PAD_UP | PAD_B, 300));
    CHECK(run(PAD_LEFT, 300) != run(PAD_RIGHT, 300));
}

static void test_ship_stays_in_pc_bounds(void)
{
    run(PAD_LEFT | PAD_UP, 400);
    CHECK(players[0].x == 40 && players[0].y == 10);
    run(PAD_RIGHT | PAD_DOWN, 400);
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
    game_init();
    for (i = 0; i < 1500; i++) game_step(PAD_B | ((i / 70) & 1 ? PAD_RIGHT : PAD_LEFT));
    CHECK(game.total_enemy > 0);
    CHECK(game.enemy_killed > 0);
    CHECK(players[0].cash > 0);
    CHECK(game.event_loc > 1);
}

static void test_events_follow_cur_loc(void)
{
    unsigned i;
    game_init();
    for (i = 0; i < 100; i++) game_step(0);
    CHECK(game.cur_loc == 100);   /* backMove 1 per tick until an event changes it */
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
    game_init();
    players[0].cash = 0;
    /* full trade-in value: selling Pulse Cannon Lv1 buys it back */
    CHECK(shop_buy(SHOP_FRONT, 1, 1) && players[0].cash == 0);
    CHECK(c3 == c1 + c1 * 1 + c1 * 3);      /* + cost*1, + cost*(1+2) */
    CHECK(!shop_buy(SHOP_FRONT, 1, 3));     /* cannot afford */
    players[0].cash = c3 - c1;
    CHECK(shop_buy(SHOP_FRONT, 1, 3) && players[0].cash == 0 && players[0].weapon_power[0] == 3);
}

int main(void)
{
    test_deterministic();
    test_ship_stays_in_pc_bounds();
    test_velocity_is_capped();
    test_pulse_cannon_fires_and_shots_leave();
    test_generator_limits_fire();
    test_level_spawns_and_kills_enemies();
    test_events_follow_cur_loc();
    test_script_walk();
    test_shop_trade_in();
    if (failures) return 1;
    printf("   PASS  test_game\n");
    return 0;
}

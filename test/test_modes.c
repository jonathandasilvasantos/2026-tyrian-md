/* Source rules for ENGAGE equipment, arcade lives and GALAGA rewards. */
#include <stdio.h>
#include "game/game.h"
#include "game/episode.h"
#include "game/player.h"
#include "game/mode.h"
#include "game/special.h"
#include "game/script.h"
#include "game/level.h"
#include "game/enemy.h"
#include "game/rng.h"
#include "game/shots.h"
#include "gen_data/level.h"
#include "gen_data/items.h"
static unsigned failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL modes:%d %s\n", __LINE__, #c); failures++; } } while (0)
static void run_event(LevelEvent event)
{
    const LevelDef *saved = levels;
    uint8_t old_level = game.level;
    LevelDef fake = levels[old_level];
    LevelEvent events[2] = {event, {.time = 65500}};
    fake.events = events; fake.event_count = 1;
    levels = &fake; game.level = 0; game.event_loc = 1; game.cur_loc = 0;
    level_run_events();
    levels = saved; game.level = old_level;
}

static void test_mode_events(void)
{
    Enemy *e = &enemies[0];
    uint32_t expected;
    unsigned i;
    game_new(1, DIFFICULTY_NORMAL);
    *e = (Enemy){0}; enemy_avail[0] = EA_ACTIVE;
    e->linknum = 7; e->enemydie = 42; e->evalue = 10;
    mode_galaga();
    CHECK(level_search_for(7, 0) && e->evalue == 20);
    CHECK(level_search_for(7, 0) && e->evalue == 40); /* original search side effect */
    game.difficulty = DIFFICULTY_EASY;
    run_event((LevelEvent){.type=25,.dat=10,.dat4=7});
    CHECK(e->armorleft == 0); /* integer difficulty/2 before the PC roundf */
    run_event((LevelEvent){.type=47,.dat=10,.dat4=7});
    CHECK(e->armorleft == 10); /* event 47 has no GALAGA multiplier */
    for (i = 0; i < 20; i++) run_event((LevelEvent){.type=78});
    CHECK(game_mode.shot_frequency == 10);
    run_event((LevelEvent){.type=82,.dat=3});
    CHECK(players[0].special_id == 3 && !players[0].shot_repeat[8] && !players[0].shot_repeat[10]);

    game_new(1, DIFFICULTY_NORMAL);
    *e = (Enemy){0}; enemy_avail[0] = EA_ACTIVE; e->linknum = 7; e->enemydie = 42;
    players[0].weapon_power[0] = 11; /* guaranteed random special, even in story event 45 */
    rng_seed(123); rng_mod(6); expected = rng_state; rng_seed(123);
    run_event((LevelEvent){.type=45,.dat=533,.dat4=7});
    CHECK(rng_state == expected && e->enemydie == 42);
    run_event((LevelEvent){.type=45,.dat=533,.dat4=7});
    CHECK(rng_state == expected); /* PC event record was rewritten on first visit */
    game_mode.arcade = 1;
    run_event((LevelEvent){.type=45,.dat=533,.dat4=7});
    CHECK(e->enemydie >= 829 && e->enemydie <= 834 && rng_state == expected);

    game_new(1, DIFFICULTY_NORMAL); mode_engage();
    *e = (Enemy){0}; enemy_avail[0] = EA_ACTIVE; e->linknum = 7;
    rng_seed(123); rng_mod(4); expected = rng_state; rng_seed(123);
    run_event((LevelEvent){.type=33,.dat=534,.dat4=7});
    CHECK(rng_state == expected && (e->enemydie == 829 || e->enemydie == 830 || e->enemydie == 832 || e->enemydie == 833));
    run_event((LevelEvent){.type=33,.dat=534,.dat4=7});
    CHECK(rng_state == expected);
    level_init(); e->enemydie = 42;
    run_event((LevelEvent){.type=33,.dat=512,.dat4=7});
    CHECK(e->enemydie == 42);

    /* A suppressed GALAGA turret skips all remaining turrets and launches. */
    game_new(1, DIFFICULTY_NORMAL); mode_galaga();
    *e = (Enemy){0}; enemy_avail[0] = EA_ACTIVE;
    e->ex = 100; e->ey = 80; e->enemycycle = 1; e->egr[0] = 1;
    e->freq[2] = e->eshotwait[2] = e->tur[2] = 1;
    e->launchfreq = e->launchwait = e->launchtype = 1;
    rng_seed(123); expected = rng_state;
    enemies_update(25, 0, 0);
    CHECK(rng_state == expected && e->launchwait == 1); /* stationary: no random sample */
    e->eyc = 1;
    rng_seed(123); rng_mod(400); expected = rng_state; rng_seed(123);
    enemies_update(25, 0, 0);
    CHECK(rng_state == expected && e->launchwait == 1); /* moving: exactly one sample */
}


static unsigned buddy_shots(void)
{
    unsigned i, count = 0;
    for (i = 0; i < PLAYER_SHOT_MAX; i++)
        if (player_shots[i].avail && player_shots[i].player == 2) count++;
    return count;
}

static void pickup(uint8_t who, int16_t value)
{
    Enemy *e = &enemies[0];
    enemies_init();
    *e = (Enemy){0};
    enemy_avail[0] = EA_ITEM;
    e->ex = players[who - 1].x; e->ey = players[who - 1].y;
    e->evalue = value; e->scoreitem = 1;
    player_collide_for(who);
    CHECK(enemy_avail[0] == EA_FREE);
}

static void test_buddy(void)
{
    Player *p = &players[0], *b = &players[1];
    unsigned i;
    game_new(1, DIFFICULTY_NORMAL); mode_engage(); mode_galaga(); game_start_level(17);
    CHECK(b->weapon_id[1] == 15 && b->weapon_power[1] == 1);
    CHECK(!b->option_id[0] && !b->option_id[1] && b->x == 190 && b->invulnerable_ticks == 100);
    pickup(1, 30000);
    CHECK(game_mode.buddy && p->cash == 100 && b->armor == 10 && b->is_alive);
    game_mode.charge = 3; game_mode.link_phase = 12345; b->invulnerable_ticks = 45;
    pickup(2, 30000);
    CHECK(b->cash == 2500 && p->cash == 100 && game_mode.charge == 3);
    CHECK(game_mode.link_phase == 12345 && b->invulnerable_ticks == 45);
    game_mode.link_phase = 0x80000000u; game_mode.charge = 0;
    p->x = 120; p->y = 100; shots_init();
    mode_buddy_tick(0);
    CHECK(b->x == p->x - (ships[b->ship].ani == 0) && b->y == 108);
    CHECK(buddy_shots() == 3 && b->invulnerable_ticks == 44);
    { static const uint16_t pads[] = {PAD_RIGHT, PAD_DOWN, PAD_LEFT, PAD_UP};
      static const uint32_t phases[] = {0x40000000u, 0, 0xC0000000u, 0x80000000u};
      for (i = 0; i < 4; i++) {
          unsigned j;
          for (j = 0; j < 32; j++) { shots_init(); mode_buddy_tick(PAD_B | pads[i]); }
          CHECK(game_mode.link_phase == phases[i]);
      }
    }
    shots_init(); game.power = 900; game_mode.charge = 5; game_mode.charge_wait = 1;
    b->shot_repeat[1] = b->shot_repeat[5] = 0;
    mode_buddy_tick(PAD_B);
    CHECK(buddy_shots() == 12 && !game_mode.charge && game_mode.charge_wait == 28);
    p->x = 40; p->y = 40; b->x = 120; b->y = 100;
    b->invulnerable_ticks = 0; b->armor = 10;
    enemy_shots[0] = (EnemyShot){.sx=120,.sy=100,.sdmg=3,.duration=10};
    enemy_shots_update();
    CHECK(enemy_shots[0].avail && b->armor == 7 && p->armor == p->initial_armor);
    p->armor = 20; b->armor = 10; pickup(2, 20005);
    CHECK(p->armor == 22 && b->armor == 12);
    pickup(1, 20999); CHECK(p->armor == 28 && b->armor == 28);
    { uint32_t cash = p->cash, buddy_cash = b->cash;
      Enemy *e = &enemies[0]; PlayerShot *shot = &player_shots[0];
      pickup(2, 50); CHECK(b->cash == buddy_cash + 50 && p->cash == cash);
      enemies_init(); shots_init();
      *e = (Enemy){0}; enemy_avail[0] = EA_ACTIVE;
      e->ex = 100; e->ey = 86; e->enemycycle = 1; e->armorleft = 1; e->evalue = 50;
      *shot = (PlayerShot){.x=100,.y=80,.gr=1,.avail=5,.ani_max=1,.player=2,.dmg=2,.trail=255};
      shots_move();
      CHECK(enemy_avail[0] == EA_FREE && p->cash == cash + 50 && b->cash == buddy_cash + 50);
    }
    b->armor = 0; mode_tick(); CHECK(!game_mode.buddy);
    mode_collect_buddy(1); p->is_alive = 0; p->weapon_power[0] = 2;
    CHECK(mode_respawn() && !game_mode.buddy);

    game_new(1, DIFFICULTY_NORMAL); game_start_level(LEVEL_FIRST);
    CHECK(p->purple_balls_needed == 1);
    { uint32_t cash = p->cash;
      pickup(1, 30000); CHECK(p->weapon_power[0] == 2 && p->purple_balls_needed == 2);
      pickup(1, 30000); CHECK(p->weapon_power[0] == 2 && p->purple_balls_needed == 1);
      pickup(1, 30000); CHECK(p->weapon_power[0] == 3 && p->purple_balls_needed == 4);
      CHECK(p->cash == cash + 300);
      pickup(1, 30000); CHECK(p->purple_balls_needed == 3);
      game_start_level(LEVEL_FIRST); CHECK(p->purple_balls_needed == 4);
    }
}

int main(void)
{
    uint8_t episode;
    test_mode_events();
    test_buddy();
    for (episode = 1; episode <= 4; episode++) {
        Player *p = &players[0];
        unsigned i;
        game_new(episode, DIFFICULTY_HARD);
        p->cash = 54321; p->ship = 2; p->weapon_power[0] = 5;
        script.save_section = 1;
        mode_checkpoint();
        mode_engage();
        CHECK(game_mode.arcade && game_mode.super_tyrian && game_mode.no_backup);
        CHECK(!p->cash && p->ship == 13 && p->weapon_id[0] == 39 && !p->weapon_id[1]);
        CHECK(p->weapon_power[0] == 3 && p->weapon_power[1] == 1);
        CHECK(!p->option_id[0] && !p->option_id[1] && !p->special_id);
        CHECK(p->generator == 2 && p->shield_item == 4);
        mode_galaga();
        game_start_level(episode == 1 || episode == 4 ? 17 : LEVEL_FIRST);
        CHECK(game.difficulty == DIFFICULTY_NORMAL && !p->shield);
        CHECK(game_mode.next_life == 10000 && !game_mode.shot_frequency);
        p->cash = 10000; p->shield = 255;
        mode_tick();
        CHECK(!p->shield && p->weapon_power[0] == 4 && game_mode.next_life == 20000);
        CHECK(game.sound_queue[6] == 11 && game.sound_queue[7] == 21);
        p->cash = 20000; mode_tick();
        CHECK(p->weapon_power[0] == 5 && game_mode.next_life == 45000);
        p->weapon_power[0] = 11; p->cash = 45000; mode_tick();
        CHECK(p->cash == 46000 && p->weapon_power[0] == 11 && game_mode.next_life == 70000);
        p->is_alive = 0; p->exploding_ticks = 0; p->weapon_power[0] = 3;
        CHECK(mode_respawn());
        CHECK(p->is_alive && p->weapon_power[0] == 2 && p->y == 160);
        CHECK(p->armor == p->initial_armor && !p->shield && p->invulnerable_ticks == 100);
        p->is_alive = 0; p->weapon_power[0] = 1;
        CHECK(!mode_respawn() && !p->is_alive);
        p->weapon_power[0] = 2; p->exploding_ticks = 1;
        player_tick(0);
        CHECK(!p->is_alive && !p->exploding_ticks && p->weapon_power[0] == 2);
        player_tick(0);
        CHECK(p->is_alive && p->weapon_power[0] == 1 && p->invulnerable_ticks == 99);
        CHECK(mode_restore());
        CHECK(!game_mode.arcade && !game_mode.galaga && !game_mode.super_tyrian);
        CHECK(p->cash == 54321 && p->ship == 2 && p->weapon_power[0] == 5);
        CHECK(script.section == 1);

        mode_engage(); game_start_level(LEVEL_FIRST);
        /* Stalker normally has no standard ship combos; SuperTyrian does. */
        special_combo(9); special_combo(5); /* Fire-burst, combo table entry 15 */
        CHECK(special_state.executed == 26);
        special_state.executed = 0;
        special_init();
        /* Last of 21 sequences, D-Media Protron Drone, is reachable too. */
        { static const uint8_t keys[] = {1,4,3,4,7};
          for (i = 0; i < sizeof(keys); i++) special_combo(keys[i]); }
        CHECK(special_state.executed == 36);
        game_mode.extra = 1; mode_galaga();
        mode_leave_level();
        CHECK(!game_mode.galaga && !game_mode.extra && !game_mode.no_backup);
        CHECK(game_mode.arcade && game_mode.super_tyrian);
    }
    if (failures) return 1;
    puts("   PASS  scripted modes (ENGAGE, lives, linked ship, purple balls, backup, 21 combos)");
    return 0;
}

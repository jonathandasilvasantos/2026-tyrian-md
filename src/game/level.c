/* level.c — level event system and background scroll state, ported from
 * OpenTyrian tyrian2.c (JE_eventSystem, JE_createNewEventEnemy,
 * JE_eventJump, JE_searchFor, the background part of JE_main) and
 * backgrnd.c (layer movement).  Single player, no arcade/galaga modes.
 * Visual-only events are kept as state for the renderer or ignored:
 * starfield (1, 8, 9) and filters (44) are simulated; window text (16),
 * music (34, 35) and sounds (62) are queued. Smoothies (64) remain omitted. */
#include "level.h"
#include "game.h"
#include "enemy.h"
#include "player.h"
#include "filter.h"
#include "stars.h"
#include "rng.h"
#include "gen_data/level.h"

#define LV (&levels[game.level])
#define level_events (LV->events)

static const int8_t layer_width[3] = { LAYER1_W, LAYER2_W, LAYER3_W };
static const int16_t layer_rows[3] = { 300, 600, 600 };

/* Event 12 rewrites its own dat6 to 0 (eventRec is RAM on the PC); a
 * re-run after a jump then spawns in group 25.  ROM events: remember which
 * event-12 records already ran. */
#define EV12_MAX 16
static uint16_t ev12_done[EV12_MAX];
static uint8_t ev12_count;

static int16_t iabs(int16_t v) { return v < 0 ? -v : v; }

void level_init(void)
{
    int l;
    for (l = 0; l < 3; l++) {
        LayerScroll *s = &game.layer[l];
        int16_t map_y = layer_rows[l] - 8;
        s->ptr = map_y * layer_width[l] - 1;
        s->wrap = s->wrap_to = layer_width[l];   /* &mainmap[1][0]: stop at the top */
        s->back_pos = 0;
        s->delay = s->delay_max = 1;
        s->wrap_rows = 0;
    }
    game.event_loc = 1;
    game.cur_loc = 0;
    game.back_move[0] = 1;
    game.back_move[1] = 2;
    game.back_move[2] = 3;
    game.explode_move = 2;
    game.enemies_active = 1;
    game.stop_backgrounds = 0;
    game.stop_background_num = 0;
    game.background3x1 = game.background3x1b = 0;
    game.background3over = 0;
    game.background2over = 1;
    game.top_enemy_over = game.sky_enemy_over_all = 0;
    game.small_enemy_adjust = 0;
    game.enemy_continual_damage = 0;
    game.level_enemy_frequency = 96;
    game.force_events = 0;
    game.super_enemy_254_jump = 0;
    game.damage_rate = 2;
    game.random_explosions = 0;
    game.return_active = 0;
    game.end_level = game.ready_to_end_level = game.really_end_level = 0;
    game.level_end = 255;
    game.level_end_warp = -4;
    game.armor_ship_delay = 50;
    game.enemy_killed = game.total_enemy = game.cube_max = 0;
    game.boss_link[0] = game.boss_link[1] = 0;
    game.level_timer = 0;
    game.level_timer_countdown = 0;
    for (l = 0; l < 10; l++) game.global_flags[l] = 0;
    ev12_count = 0;
}

int16_t level_layer_row(int layer)
{
    return (game.layer[layer].ptr + 1) / layer_width[layer];
}

/* Move a layer's map pointer, tracking the rows skipped for the renderer's
 * continuous (virtual) scroll. */
static void set_ptr(int layer, int16_t ptr)
{
    LayerScroll *s = &game.layer[layer];
    int16_t before = level_layer_row(layer);
    s->ptr = ptr;
    s->wrap_rows += before - level_layer_row(layer);
}

void level_wrap(void)
{
    int l;
    for (l = 0; l < 3; l++)
        if (game.layer[l].ptr <= game.layer[l].wrap) set_ptr(l, game.layer[l].wrap_to);
}

static void advance(int layer, uint8_t move)
{
    LayerScroll *s = &game.layer[layer];
    s->back_pos += move;
    if (s->back_pos > 27) {
        s->back_pos -= 28;
        s->ptr -= layer_width[layer];
    }
}

void level_scroll_1(void)
{
    LayerScroll *s = &game.layer[0];
    if (game.force_events && !game.back_move[0]) game.cur_loc++;
    if (s->delay_max > 1 && game.back_move[0] < 2) game.back_move[0] = s->delay == 1 ? 1 : 0;
    if (--s->delay == 0) {
        s->delay = s->delay_max;
        game.cur_loc += game.back_move[0];
        advance(0, game.back_move[0]);
    }
}

void level_scroll_2(void)
{
    LayerScroll *s = &game.layer[1];
    if (game.background2over == 2) return;   /* draw_background_2 not called: no movement */
    if (s->delay_max > 1 && game.back_move[1] < 2) game.back_move[1] = s->delay == 1 ? 1 : 0;
    if (--s->delay == 0) {
        s->delay = s->delay_max;
        advance(1, game.back_move[1]);
    }
}

void level_scroll_3(void)
{
    advance(2, game.back_move[2]);
}

void level_event_jump(uint16_t jump)
{
    uint16_t t = 0;
    if (jump == 65535) {
        game.cur_loc = game.return_loc;
    } else {
        game.return_loc = game.cur_loc + 1;
        game.cur_loc = jump;
    }
    do {
        t++;
    } while (!(level_events[t - 1].time >= game.cur_loc));
    game.event_loc = t - 1;
}

uint8_t level_search_for(uint8_t link, uint8_t *out)
{
    int i, found = -1;
    for (i = 0; i < ENEMY_MAX; i++)
        if (enemy_avail[i] == EA_ACTIVE && enemies[i].linknum == link) found = i;
    if (found < 0) return 0;
    if (out) *out = (uint8_t)found;
    return 1;
}

/* JE_createNewEventEnemy; returns slot+1 or 0 (the PC's global b). */
static int create_event_enemy(const LevelEvent *ev, uint8_t type_ofs, int offset, int16_t unique_bank)
{
    int i, b = 0;
    Enemy *e;
    for (i = offset; i < offset + 25; i++) {
        if (enemy_avail[i] == EA_FREE) {
            b = i + 1;
            break;
        }
    }
    if (!b) return 0;
    e = &enemies[b - 1];
    enemy_avail[b - 1] = enemy_make(e, (uint16_t)(ev->dat + type_ofs), unique_bank);
    if (ev->dat2 != -99) {
        switch (offset) {
        case 0:
            e->ex = ev->dat2 - (LV->map_x - 1) * 24;
            e->ey -= game.back_move[1];
            break;
        case 25:
        case 75:
            e->ex = ev->dat2 - (LV->map_x - 1) * 24 - 12;
            e->ey -= game.back_move[0];
            break;
        case 50:
            if (game.background3x1)
                e->ex = ev->dat2 - (LV->map_x - 1) * 24 - 12;
            else
                e->ex = ev->dat2 - LV->map_x3 * 24 - 24 * 2 + 6;
            e->ey -= game.back_move[2];
            if (game.background3x1b) e->ex -= 6;
            break;
        }
        e->ey = -28;
        if (game.background3x1b && offset == 50) e->ey += 4;
    }
    if (game.small_enemy_adjust && e->size == 0) {
        e->ex -= 10;
        e->ey -= 7;
    }
    e->ey += ev->dat5;
    e->eyc += ev->dat3;
    e->linknum = ev->dat4;
    e->fixedmovey = ev->dat6;
    e->px = e->ex;     /* first drawn after its first update */
    e->py = e->ey;
    e->moved = 0;
    return b;
}

/* dat4, or newPL[dat3 - 80] for dat3 in 80..89 (events 19, 20, 27, 55) */
static uint8_t link_target(const LevelEvent *ev)
{
    return (ev->dat3 > 79 && ev->dat3 < 90) ? game.new_pl[ev->dat3 - 80] : ev->dat4;
}

static void event_system(void)
{
    const LevelEvent *ev = &level_events[game.event_loc - 1];
    int i, b;

    switch (ev->type) {
    case 1: starfield.speed = ev->dat; break;
    case 8: starfield.active = 0; break;
    case 9: starfield.active = 1; break;

    case 2:
    case 30:
        game.layer[0].delay = game.layer[0].delay_max = 1;
        game.layer[1].delay = game.layer[1].delay_max = 1;
        game.back_move[0] = (uint8_t)ev->dat;
        game.back_move[1] = (uint8_t)ev->dat2;
        if (ev->type == 30)
            game.explode_move = (uint8_t)ev->dat2;
        else
            game.explode_move = ev->dat2 > 0 ? (uint8_t)ev->dat2 : (uint8_t)ev->dat;
        game.back_move[2] = (uint8_t)ev->dat3;
        if (ev->type == 2 && game.back_move[0] > 0) game.stop_background_num = 0;
        break;
    case 3:
        game.back_move[0] = 1;
        game.layer[0].delay = game.layer[0].delay_max = 3;
        game.back_move[1] = 1;
        game.layer[1].delay = game.layer[1].delay_max = 2;
        game.back_move[2] = 1;
        break;
    case 4:
        game.stop_backgrounds = 1;
        game.stop_background_num = ev->dat <= 1 ? 1 : (uint8_t)ev->dat;
        break;
    case 5:
        enemy_bank_ids[0] = ev->dat > 0 ? (uint8_t)ev->dat : 0;
        enemy_bank_ids[1] = ev->dat2 > 0 ? (uint8_t)ev->dat2 : 0;
        enemy_bank_ids[2] = ev->dat3 > 0 ? (uint8_t)ev->dat3 : 0;
        enemy_bank_ids[3] = ev->dat4;
        break;
    case 6: create_event_enemy(ev, 0, 25, 0); break;
    case 7: create_event_enemy(ev, 0, 50, 0); break;
    case 10: create_event_enemy(ev, 0, 75, 0); break;
    case 11:
        if (ev->dat == 1) {
            game.really_end_level = 1;
        } else if (!game.end_level) {
            game.ready_to_end_level = 0;
            game.end_level = 1;
            game.level_end = 40;
        }
        break;
    case 12: {
        int8_t dat6 = ev->dat6;
        int g = 25;
        for (i = 0; i < ev12_count; i++)
            if (ev12_done[i] == game.event_loc) dat6 = 0;
        if (dat6 != 0 && ev12_count < EV12_MAX) ev12_done[ev12_count++] = game.event_loc;
        switch (dat6) {
        case 2: g = 0; break;
        case 3: g = 50; break;
        case 4: g = 75; break;
        }
        {
            LevelEvent e = *ev;
            e.dat6 = 0;     /* "we use EVENTDAT6 for the background" */
            create_event_enemy(&e, 0, g, 0);
            b = create_event_enemy(&e, 1, g, 0);
            if (b) enemies[b - 1].ex += 24;
            b = create_event_enemy(&e, 2, g, 0);
            if (b) enemies[b - 1].ey -= 28;
            b = create_event_enemy(&e, 3, g, 0);
            if (b) {
                enemies[b - 1].ex += 24;
                enemies[b - 1].ey -= 28;
            }
        }
        break;
    }
    case 13: game.enemies_active = 0; break;
    case 14: game.enemies_active = 1; break;
    case 15: create_event_enemy(ev, 0, 0, 0); break;
    case 17:
        b = create_event_enemy(ev, 0, 25, 0);
        if (b) enemies[b - 1].ey = 190 + ev->dat5;
        break;
    case 18:
        b = create_event_enemy(ev, 0, 0, 0);
        if (b) enemies[b - 1].ey = 190 + ev->dat5;
        break;
    case 19: {
        int lo = 0, hi = 100, all = 0;
        uint8_t link = ev->dat4;
        if (ev->dat3 > 79 && ev->dat3 < 90) {
            link = game.new_pl[ev->dat3 - 80];
        } else {
            switch (ev->dat3) {
            case 2: lo = 0; hi = 25; all = 1; break;
            case 1: lo = 25; hi = 50; all = 1; break;
            case 3: lo = 50; hi = 75; all = 1; break;
            case 99: all = 1; break;
            }
        }
        for (i = lo; i < hi; i++) {
            Enemy *e = &enemies[i];
            if (!all && e->linknum != link) continue;
            if (ev->dat != -99) e->exc = (int8_t)ev->dat;
            if (ev->dat2 != -99) e->eyc = (int8_t)ev->dat2;
            if (ev->dat6 != 0) e->fixedmovey = ev->dat6;
            if (ev->dat6 == -99) e->fixedmovey = 0;
            if (ev->dat5 > 0) e->enemycycle = (uint8_t)ev->dat5;
        }
        break;
    }
    case 20: {
        uint8_t link = link_target(ev);
        for (i = 0; i < ENEMY_MAX; i++) {
            Enemy *e = &enemies[i];
            if (enemy_avail[i] == EA_FREE || !(e->linknum == link || link == 0)) continue;
            if (ev->dat != -99) {
                e->excc = (int8_t)ev->dat;
                e->exccw = (int8_t)iabs(ev->dat);
                e->exccwmax = (uint8_t)iabs(ev->dat);
                e->exccadd = ev->dat > 0 ? 1 : -1;
            }
            if (ev->dat2 != -99) {
                e->eycc = (int8_t)ev->dat2;
                e->eyccw = (int8_t)iabs(ev->dat2);
                e->eyccwmax = (uint8_t)iabs(ev->dat2);
                e->eyccadd = ev->dat2 > 0 ? 1 : -1;
            }
            if (ev->dat5 > 0) e->enemycycle = (uint8_t)ev->dat5;
            if (ev->dat6 > 0) {
                e->ani = (uint8_t)ev->dat6;
                e->animin = (uint8_t)ev->dat5;
                e->animax = 0;
                e->aniactive = 1;
            }
        }
        break;
    }
    case 21: game.background3over = 1; break;
    case 22: game.background3over = 0; break;
    case 23:
        b = create_event_enemy(ev, 0, 50, 0);
        if (b) enemies[b - 1].ey = 180 + ev->dat5;
        break;
    case 24:
        for (i = 0; i < ENEMY_MAX; i++) {
            Enemy *e = &enemies[i];
            if (e->linknum != ev->dat4) continue;
            e->aniactive = 1;
            e->aniwhenfire = 0;
            if (ev->dat2 > 0) {
                e->enemycycle = (uint8_t)ev->dat2;
                e->animin = e->enemycycle;
            } else {
                e->enemycycle = 0;
            }
            if (ev->dat > 0) e->ani = (uint8_t)ev->dat;
            if (ev->dat3 == 1) {
                e->animax = e->ani;
            } else if (ev->dat3 == 2) {
                e->aniactive = 2;
                e->animax = e->ani;
                e->aniwhenfire = 2;
            }
        }
        break;
    case 25:
    case 47:
        for (i = 0; i < ENEMY_MAX; i++)
            if (ev->dat4 == 0 || enemies[i].linknum == ev->dat4) enemies[i].armorleft = (uint8_t)ev->dat;
        break;
    case 26: game.small_enemy_adjust = (uint8_t)ev->dat; break;
    case 27: {
        uint8_t link = link_target(ev);
        for (i = 0; i < ENEMY_MAX; i++) {
            Enemy *e = &enemies[i];
            if (!(link == 0 || e->linknum == link)) continue;
            if (ev->dat != -99) e->exrev = (int8_t)ev->dat;
            if (ev->dat2 != -99) e->eyrev = (int8_t)ev->dat2;
            if (ev->dat3 != 0 && ev->dat3 < 17) e->filter = (uint8_t)ev->dat3;
        }
        break;
    }
    case 28: game.top_enemy_over = 0; break;
    case 29: game.top_enemy_over = 1; break;
    case 31:
        for (i = 0; i < ENEMY_MAX; i++) {
            Enemy *e = &enemies[i];
            if (!(ev->dat4 == 99 || e->linknum == ev->dat4)) continue;
            e->freq[0] = (uint8_t)ev->dat;
            e->freq[1] = (uint8_t)ev->dat2;
            e->freq[2] = (uint8_t)ev->dat3;
            e->eshotwait[0] = e->eshotwait[1] = e->eshotwait[2] = 1;
            if (e->launchtype > 0) {
                e->launchfreq = (uint8_t)ev->dat5;
                e->launchwait = 1;
            }
        }
        break;
    case 32:
        b = create_event_enemy(ev, 0, 50, 0);
        if (b) enemies[b - 1].ey = 190;
        break;
    case 33: {
        /* the PC rewrites dat (533 -> random special) in place; ours is local */
        /* "lives" aliases the front weapon power in a story game
         * (mainint.c: lives = &items.weapon[p].power) */
        uint16_t die = (uint16_t)ev->dat;
        uint8_t lives = players[0].weapon_power[FRONT_WEAPON];
        if (die == 533 && (lives == 11 || rng_mod(15) < lives))
            die = 829 + rng_mod(6);   /* drops a random special weapon */
        for (i = 0; i < ENEMY_MAX; i++)
            if (enemies[i].linknum == ev->dat4) enemies[i].enemydie = die;
        break;
    }
    case 36: game.ready_to_end_level = 1; break;
    case 37: game.level_enemy_frequency = (uint8_t)ev->dat; break;
    case 38: {
        uint16_t t, loc = 1;
        game.cur_loc = (uint16_t)ev->dat;
        for (t = 0; t < LV->event_count; t++)
            if (level_events[t].time <= game.cur_loc) loc = t;
        game.event_loc = loc;
        break;
    }
    case 39:
        for (i = 0; i < ENEMY_MAX; i++)
            if (enemies[i].linknum == ev->dat) enemies[i].linknum = (uint8_t)ev->dat2;
        break;
    case 40: game.enemy_continual_damage = 1; break;
    case 41:
        for (i = 0; i < (ev->dat == 0 ? ENEMY_MAX : 25); i++) enemy_avail[i] = EA_FREE;
        break;
    case 42: game.background3over = 2; break;
    case 43: game.background2over = (uint8_t)ev->dat; break;
    case 44:
        filter_state.active = ev->dat > 0;
        filter_state.fade = ev->dat == 2;
        filter_state.hue = ev->dat2; filter_state.brightness = ev->dat3;
        filter_state.next_hue = ev->dat4; filter_state.delta = ev->dat5;
        filter_state.fade_start = ev->dat6 == 0;
        break;
    case 45: {
        /* arcade/2-player only: a story game only consumes the RNG */
        if (ev->dat == 533 && players[0].weapon_power[FRONT_WEAPON] != 11) rng_mod(15);
        break;
    }
    case 46:
        if (ev->dat3 != 0) game.damage_rate = (uint8_t)ev->dat3;
        if (ev->dat2 == 0) {
            game.difficulty += ev->dat;
            if (game.difficulty < DIFFICULTY_EASY) game.difficulty = DIFFICULTY_EASY;
            if (game.difficulty > DIFFICULTY_10) game.difficulty = DIFFICULTY_10;
        }
        break;
    case 49: case 50: case 51: case 52: {
        static const uint8_t groups[4] = { 25, 0, 50, 75 };
        LevelEvent e = *ev;
        game.custom_armor = (uint8_t)ev->dat6;
        game.custom_egr = (uint16_t)ev->dat;
        e.dat = 0;
        e.dat3 = 0;
        e.dat6 = 0;
        create_event_enemy(&e, 0, groups[ev->type - 49], ev->dat3);
        break;
    }
    case 53: game.force_events = ev->dat != 99; break;
    case 54: level_event_jump((uint16_t)ev->dat); break;
    case 55: {
        uint8_t link = link_target(ev);
        for (i = 0; i < ENEMY_MAX; i++) {
            Enemy *e = &enemies[i];
            if (!(link == 0 || e->linknum == link)) continue;
            if (ev->dat != -99) e->xaccel = (uint8_t)ev->dat;
            if (ev->dat2 != -99) e->yaccel = (uint8_t)ev->dat2;
        }
        break;
    }
    case 56:
        b = create_event_enemy(ev, 0, 75, 0);
        if (b) enemies[b - 1].ey = 190;
        break;
    case 57: game.super_enemy_254_jump = (uint16_t)ev->dat; break;
    case 60:
        for (i = 0; i < ENEMY_MAX; i++) {
            Enemy *e = &enemies[i];
            if (e->linknum != ev->dat4) continue;
            e->special = 1;
            e->flagnum = (uint8_t)ev->dat;
            e->setto = ev->dat2 == 1;
        }
        break;
    case 61:
        if (ev->dat >= 1 && ev->dat <= 10 && game.global_flags[ev->dat - 1] == ev->dat2)
            game.event_loc += ev->dat3;
        break;
    case 16: {
        /* window text (JE_drawTextWindow) and its voice (sndmast.c) */
        static const uint8_t samples[9] = { 35, 31, 32, 30, 35, 36, 38, 35, 32 };
        if (ev->dat >= 1 && ev->dat <= 9) {
            game.sound_queue[3] = samples[ev->dat - 1];
            game.window_text = (uint8_t)ev->dat;
        }
        break;
    }
    case 62: game.sound_queue[3] = (uint8_t)ev->dat; break;
    case 63: game.event_loc += ev->dat; break;     /* not 2-player */
    case 65: game.background3x1 = ev->dat == 0; break;
    case 66:
        if (game.initial_difficulty <= ev->dat) game.event_loc += ev->dat2;
        break;
    case 67:
        game.level_timer = ev->dat == 1;
        game.level_timer_countdown = (uint16_t)(ev->dat3 * 100);
        game.level_timer_jump_to = (uint16_t)ev->dat2;
        break;
    case 68: game.random_explosions = ev->dat == 1; break;
    case 69: players[0].invulnerable_ticks = (uint8_t)ev->dat; break;
    case 70:
        if (ev->dat2 == 0) {
            uint8_t found = 0;
            for (i = 1; i <= 19; i++) found |= level_search_for((uint8_t)i, 0);
            if (!found) level_event_jump((uint16_t)ev->dat);
        } else if (!level_search_for((uint8_t)ev->dat2, 0) &&
                   (ev->dat3 == 0 || !level_search_for((uint8_t)ev->dat3, 0)) &&
                   (ev->dat4 == 0 || !level_search_for(ev->dat4, 0))) {
            level_event_jump((uint16_t)ev->dat);
        }
        break;
    case 71:
        if (game.layer[0].ptr * 2 <= ev->dat2) level_event_jump((uint16_t)ev->dat);
        break;
    case 72: game.background3x1b = ev->dat == 1; break;
    case 73: game.sky_enemy_over_all = ev->dat == 1; break;
    case 74:
        for (i = 0; i < ENEMY_MAX; i++) {
            Enemy *e = &enemies[i];
            if (!(ev->dat4 == 0 || e->linknum == ev->dat4)) continue;
            if (ev->dat5 != -99) e->xminbounce = ev->dat5;
            if (ev->dat6 != -99) e->yminbounce = ev->dat6;
            if (ev->dat != -99) e->xmaxbounce = ev->dat;
            if (ev->dat2 != -99) e->ymaxbounce = ev->dat2;
        }
        break;
    case 75: {
        uint8_t any = 0, idx;
        for (i = 0; i < ENEMY_MAX; i++)
            if (enemy_avail[i] == EA_ACTIVE && enemies[i].eyc == 0 &&
                enemies[i].linknum >= ev->dat && enemies[i].linknum <= ev->dat2)
                any = 1;
        if (any) {
            uint16_t t;
            do {
                t = rng_mod((uint16_t)(ev->dat2 + 1 - ev->dat)) + ev->dat;
            } while (!(level_search_for((uint8_t)t, &idx) && enemies[idx].eyc == 0));
            game.new_pl[ev->dat3 - 80] = (uint8_t)t;
        } else {
            game.new_pl[ev->dat3 - 80] = 255;
            if (ev->dat4 > 0) {
                game.cur_loc = level_events[game.event_loc - 1 + ev->dat4].time - 1;
                game.event_loc += ev->dat4 - 1;
            }
        }
        break;
    }
    case 76: game.return_active = 1; break;
    case 77:
        set_ptr(0, ev->dat / 2);
        set_ptr(1, (ev->dat2 > 0 ? ev->dat2 : ev->dat) / 2);
        break;
    case 79:
        game.boss_link[0] = (uint8_t)ev->dat;
        game.boss_link[1] = (uint8_t)ev->dat2;
        break;
    case 81:
        game.layer[1].wrap = ev->dat / 2;
        game.layer[1].wrap_to = ev->dat2 / 2;
        break;
    default:
        break;   /* visual/audio-only or unused types */
    }
    game.event_loc++;
}

void level_run_events(void)
{
    while (level_events[game.event_loc - 1].time <= game.cur_loc && game.event_loc <= LV->event_count)
        event_system();
}

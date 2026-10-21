#include "special.h"
#include "game.h"
#include "player.h"
#include "enemy.h"
#include "shots.h"
#include "rng.h"
#include "sidekick.h"
#include "filter.h"
#include "gen_data/items.h"

SpecialState special_state;

void special_init(void)
{
    special_state = (SpecialState){0};
}

uint8_t special_equip(uint8_t id)
{
    Player *p = &players[0];
    if (!id || id > SPECIAL_NUM || specials[id].type < 1 || specials[id].type > 18) return 0;
    p->special_id = id;
    p->shot_multi_pos[8] = p->shot_multi_pos[10] = 0;
    p->shot_repeat[8] = 10; p->shot_repeat[10] = 0;
    return 1;
}

static void activate(uint8_t id, uint8_t cost)
{
    const Special *s = &specials[id];
    SpecialState *st = &special_state;
    Player *p = &players[0];
    uint8_t power = p->weapon_power[0];
    unsigned i;
    st->next_wait = 0;
    switch (s->type) {
    case 1:
        shot_create(0, 10, p->x, p->y, s->weapon, 1);
        p->shot_repeat[8] = p->shot_repeat[10];
        break;
    case 2:
        for (i = 0; i < ENEMY_SHOT_MAX; i++) {
            EnemyShot *shot = &enemy_shots[i];
            if (shot->avail) continue;
            if (p->x != shot->sx) shot->sxm += p->x > shot->sx ? -1 : 1;
            if (p->y != shot->sy) shot->sym += p->y > shot->sy ? -1 : 1;
        }
        break;
    case 3:
        st->zinglon = 50; p->shot_repeat[8] = 100;
        game.sound_queue[7] = 21; /* S_SOUL_OF_ZINGLON */
        break;
    case 4:
        for (i = 0; i < ENEMY_MAX; i++) {
            Enemy *e = &enemies[i];
            if (enemy_avail[i] == EA_FREE || !e->scoreitem || !e->evalue) continue;
            if (p->x != e->ex) e->exc += p->x > e->ex ? 1 : -1;
            if (p->y != e->ey) e->eyc += p->y > e->ey ? 1 : -1;
        }
        break;
    case 12:
        p->invulnerable_ticks = (uint16_t)cost * 10;
        break;
    case 13:
        p->armor += cost / 4 + 1;
        if (p->armor > 28) p->armor = 28;
        game.sound_queue[3] = S_POWERUP;
        break;
    case 14: /* Repair P2 has no visible recipient in the single-player engine. */
        game.sound_queue[3] = S_POWERUP;
        break;
    case 17: case 18: {
        uint8_t side = s->type == 18 || p->option_id[0] == s->weapon;
        p->option_id[side] = s->weapon;
        /* PC indexes this reset with LEFT/RIGHT_SIDEKICK (0/1), not the
         * shot-bay enum (2/3); retain that behavior. */
        p->shot_multi_pos[side] = 0;
        sidekicks_refresh();
        game.sound_queue[s->type == 18 ? 4 : 3] = S_POWERUP;
        break;
    }
    default:
        if (!st->flare) st->flare_start = 1;
        st->weapon = s->weapon; st->linked = 0; st->spray = 0; st->filter = -99;
        switch (s->type) {
        case 5: st->filter = 7; st->frequency = 2; st->flare = 50; break;
        case 6: st->filter = 1; st->frequency = 7; st->flare = 200 + 25 * power; break;
        case 7:
            st->filter = 3; st->frequency = 3; st->flare = 50 + 10 * power;
            st->zinglon = 50; p->shot_repeat[8] = 100; game.sound_queue[7] = 21;
            break;
        case 8: st->frequency = 7; st->flare = 10 + power; break;
        case 9:
            st->frequency = 8; st->flare = 8 + 2 * power; st->linked = 1;
            st->next_wait = s->power; break;
        case 10: st->frequency = 8; st->flare = 14 + 4 * power; st->linked = 1; break;
        case 16:
            st->frequency = 8; st->flare = (uint16_t)cost * 16 + 8;
            st->linked = st->spray = 1; break;
        case 11:
            st->frequency = s->power; st->flare = 10 + 10 * power;
            st->astral = 20 + 10 * power; break;
        }
        break;
    }
}

void special_tick(uint16_t pad)
{
    Player *p = &players[0];
    SpecialState *st = &special_state;
    if (p->shot_repeat[8]) p->shot_repeat[8]--;
    if (st->wait) st->wait--;
    if (st->executed && !p->shot_repeat[8] && !st->flare) {
        uint8_t cost = specials[st->executed].power, affordable = 1;
        if (cost) {
            if (cost < 98) {
                if (p->shield >= cost) p->shield -= cost;
                else affordable = 0;
            } else if (cost == 98) {
                if (p->shield < 4) affordable = 0;
                cost = p->shield; p->shield = 0;
            } else if (cost == 99) {
                cost = p->shield >> 1; p->shield = cost;
            } else {
                cost -= 100;
                if (p->armor > cost) p->armor -= cost;
                else affordable = 0;
            }
        }
        p->shot_multi_pos[8] = p->shot_multi_pos[10] = 0;
        if (affordable) activate(st->executed, cost);
        st->executed = 0;
    }
    if (p->special_id) {
        if (!(pad & PAD_B)) st->held = 0;
        else if (!p->shot_repeat[8] && !st->held && !st->flare && !st->wait) {
            st->held = 1;
            activate(p->special_id, 0);
        }
    }
    if (st->astral) st->astral--;
    if (st->flare > 1) {
        if (st->filter != -99) {
            int8_t change;
            FilterState *f = &filter_state;
            if (f->hue == -99 && f->brightness == -99) f->active = 0;
            if (!f->active) {
                f->hue = st->filter;
                if (f->hue == 7) f->brightness = 0;
                f->active = 1;
            }
            change = rng_mod(2) == 0 ? -1 : 1;
            if (f->hue == 7) {
                if (f->brightness < -6) change = 1;
                if (f->brightness > 6) change = -1;
                f->brightness += change;
            }
        }
        if (rng_mod(6) < st->frequency) {
            int shot = PLAYER_SHOT_MAX;
            if (st->linked) {
                if (!p->shot_repeat[8]) shot = shot_create(0, 8, p->x, p->y, st->weapon, 1);
            } else {
                int16_t x = rng_mod(280), y = rng_mod(180);
                shot = shot_create(0, 8, x, y, st->weapon, 1);
            }
            if (st->spray && shot != PLAYER_SHOT_MAX) {
                player_shots[shot].xm = rng_mod(5) - 2;
                player_shots[shot].ym = rng_mod(5) - 2;
                if (!player_shots[shot].ym) player_shots[shot].ym++;
            }
        }
        if (--st->flare == 1) st->wait = st->next_wait;
    } else if (st->flare_start) {
        st->flare_start = 0; st->flare = 0;
        p->shot_repeat[8] = st->linked ? 15 : 200;
        if (filter_state.hue == st->filter) {
            filter_state.hue = filter_state.brightness = -99;
            filter_state.active = 0;
        }
    }
    if (st->zinglon > 1) {
        st->zinglon--;
        if (st->zinglon % 5 == 0) st->pulse = 1;
    }
}

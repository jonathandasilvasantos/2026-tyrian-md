#include "special.h"
#include "game.h"
#include "player.h"
#include "enemy.h"
#include "shots.h"
#include "rng.h"
#include "gen_data/items.h"

SpecialState special_state;

void special_init(void)
{
    special_state = (SpecialState){0};
}

uint8_t special_equip(uint8_t id)
{
    Player *p = &players[0];
    if (!id || id > SPECIAL_NUM || specials[id].type < 1 || specials[id].type > 11) return 0;
    p->special_id = id;
    p->shot_multi_pos[8] = p->shot_multi_pos[10] = 0;
    p->shot_repeat[8] = 10; p->shot_repeat[10] = 0;
    return 1;
}

static void activate(uint8_t id)
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
    default:
        if (!st->flare) st->flare_start = 1;
        st->weapon = s->weapon; st->linked = 0; st->filter = -99;
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
    if (p->special_id) {
        if (!(pad & PAD_B)) st->held = 0;
        else if (!p->shot_repeat[8] && !st->held && !st->flare && !st->wait) {
            st->held = 1;
            activate(p->special_id);
        }
    }
    if (st->astral) st->astral--;
    if (st->flare > 1) {
        /* Keep the PC's filter-flicker RNG draw, even while its palette
         * brightness effect awaits the hardware renderer. */
        if (st->filter != -99) (void)rng_mod(2);
        if (rng_mod(6) < st->frequency) {
            if (st->linked) {
                if (!p->shot_repeat[8]) shot_create(0, 8, p->x, p->y, st->weapon, 1);
            } else {
                int16_t x = rng_mod(280), y = rng_mod(180);
                shot_create(0, 8, x, y, st->weapon, 1);
            }
        }
        if (--st->flare == 1) st->wait = st->next_wait;
    } else if (st->flare_start) {
        st->flare_start = 0; st->flare = 0;
        p->shot_repeat[8] = st->linked ? 15 : 200;
    }
    if (st->zinglon > 1) {
        st->zinglon--;
        if (st->zinglon % 5 == 0) st->pulse = 1;
    }
}

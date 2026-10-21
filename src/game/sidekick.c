#include "sidekick.h"
#include "game.h"
#include "player.h"
#include "shots.h"
#include "enemy.h"
#include "gen_data/items.h"
#include "gen_data/motion.h"

static int16_t abs16(int16_t n) { return n < 0 ? -n : n; }

void sidekicks_init(void)
{
    Player *p = &players[0];
    unsigned i;
    for (i = 0; i < 20; i++) {
        p->old_x[i] = p->x - (19 - i);
        p->old_y[i] = p->y - 18;
    }
    p->attachment_move = 0;
    p->attachment_linked = 1;
    p->attachment_return = 0;
    sidekicks_refresh();
    for (i = 0; i < 2; i++) {
        p->sidekick[i].px = p->sidekick[i].x = p->x + (i ? 16 : -14);
        p->sidekick[i].py = p->sidekick[i].y = p->y;
    }
}

void sidekicks_refresh(void)
{
    Player *p = &players[0];
    unsigned i;
    for (i = 0; i < 2; i++) {
        const Option *o = &options[p->option_id[i]];
        Sidekick *s = &p->sidekick[i];
        s->ammo = s->ammo_max = o->ammo;
        s->refill_ticks = s->refill_max = (105 - o->ammo) * 4;
        s->style = o->style;
        s->animation_enabled = o->animation == 1;
        s->animation_frame = s->charge = 0;
        s->charge_ticks = 20;
    }
}

void sidekicks_before_move(void)
{
    Player *p = &players[0];
    unsigned i;
    if (!p->option_id[0] && !p->option_id[1]) return;
    for (i = 0; i < 2; i++) {
        Sidekick *s = &p->sidekick[i];
        s->px = s->x;
        s->py = s->y;
        /* Standard sidekicks use mouseX/Y: the ship's tick-start position. */
        if (s->style == 0 && p->is_alive && !game.end_level) {
            s->x = p->x + (i ? 16 : -14);
            s->y = p->y;
        }
    }
}

void sidekicks_record_move(int16_t old_x, int16_t old_y)
{
    Player *p = &players[0];
    unsigned i;
    if (p->x == old_x && p->y == old_y) return;
    /* The PC records after movement, before playfield clipping. */
    for (i = 1; i < 20; i++) {
        p->old_x[i - 1] = p->old_x[i];
        p->old_y[i - 1] = p->old_y[i];
    }
    p->old_x[19] = p->x;
    p->old_y[19] = p->y;
}

static void attachment_move(uint16_t pad)
{
    Player *p = &players[0];
    Sidekick *s = &p->sidekick[1];
    uint8_t button = (pad & PAD_Z) || ((pad & PAD_THREE_BUTTON) && (pad & PAD_A));
    if (!p->attachment_linked) {
        s->y += p->attachment_move / 2;
        if (p->attachment_move >= -2) {
            int16_t step = p->attachment_return ? 2 : 0;
            if (s->y > p->y - 20 + 5) {
                step = 2;
                p->attachment_move -= 1 + p->attachment_return;
            } else if (s->y > p->y - 20) {
                step = 3;
                p->attachment_move += p->attachment_move > 0 ? -1 : 1;
            } else if (s->y > p->y - 20 - 5) {
                step = 2;
                p->attachment_move++;
            } else if (p->attachment_move < 2 + p->attachment_return * 4) {
                p->attachment_move += 1 + p->attachment_return;
            }
            if (p->attachment_return) step *= 2;
            if (abs16(s->x - p->x) < step) step = 1;
            if (s->x > p->x) s->x -= step;
            else if (s->x < p->x) s->x += step;
            if (abs16(s->y - (p->y - 20)) + abs16(s->x - p->x) < 8) {
                p->attachment_linked = 1;
                game.sound_queue[2] = 23; /* S_CLINK */
            }
            if (button) p->attachment_return = 1;
        } else {
            p->attachment_move += 1 + p->attachment_return;
            explosion_setup(s->x + 1, s->y + 10, 0, 0, 0, 0);
        }
    } else {
        s->x = p->x;
        s->y = p->y - 20;
        if (button) {
            p->attachment_linked = p->attachment_return = 0;
            p->attachment_move = -20;
            game.sound_queue[3] = 26; /* S_WEAPON_26 */
        }
    }
    if (s->y < 10) s->y = 10;
}

void sidekicks_tick(uint16_t pad)
{
    Player *p = &players[0];
    unsigned i;
    if (!p->option_id[0] && !p->option_id[1]) return;
    if (p->sidekick[0].style == 4 && p->sidekick[1].style == 4)
        p->orbit_phase += 136713055u; /* 0.2 rad/tick, 2^32 units/turn */
    else if (p->sidekick[0].style == 4 || p->sidekick[1].style == 4)
        p->orbit_phase += 102534791u; /* 0.15 rad/tick; see agents.md §8 */
    for (i = 0; i < 2; i++) {
        const Option *o = &options[p->option_id[i]];
        Sidekick *s = &p->sidekick[i];
        uint8_t bay = i ? SHOT_RIGHT_SIDEKICK : SHOT_LEFT_SIDEKICK;
        uint8_t button = i ? ((pad & PAD_Z) || ((pad & PAD_THREE_BUTTON) && (pad & PAD_A))) : (pad & PAD_A) != 0;
        if (!p->option_id[i]) continue;
        switch (s->style) {
        case 1: case 3:
            s->x = p->old_x[i ? 0 : 9];
            s->y = p->old_y[i ? 0 : 9];
            break;
        case 2:
            if (i) attachment_move(pad);
            else {
                s->x = p->x;
                s->y = p->y > 30 ? p->y - 20 : 10;
            }
            break;
        case 4: {
            uint16_t phase = ((p->orbit_phase + 0x200000u) >> 22) & 1023;
            int16_t dx = orbit_sine[phase], dy = orbit_sine[(phase + 256) & 1023];
            s->x = p->x + (i ? -dx : dx);
            s->y = p->y + (i ? -dy : dy);
            break;
        }
        }
        if (!o->port) continue;
        if (p->shot_repeat[bay]) {
            p->shot_repeat[bay]--;
            continue;
        }
        if (s->ammo_max) {
            if (s->refill_ticks) s->refill_ticks--;
            else {
                s->refill_ticks = s->refill_max;
                if (s->ammo < s->ammo_max) s->ammo++;
            }
        }
        if ((s->ammo_max ? button && s->ammo : ((pad & PAD_B) || button))) {
            /* PC consumes ammo/charge even if power/shot slots prevent creation. */
            shot_create(o->port, bay, s->x, s->y, o->weapon + s->charge, 1);
            if (s->ammo_max) s->ammo--;
            if (s->charge) {
                p->shot_multi_pos[bay] = 0;
                s->charge = 0;
            }
            s->charge_ticks = 20;
            s->animation_enabled = 1;
        }
    }
}

void sidekicks_animate(void)
{
    Player *p = &players[0];
    unsigned i;
    if (!p->option_id[0] && !p->option_id[1]) return;
    for (i = 0; i < 2; i++) {
        const Option *o = &options[p->option_id[i]];
        Sidekick *s = &p->sidekick[i];
        if (!p->option_id[i]) continue;
        if (o->animation && s->animation_enabled && ++s->animation_frame >= o->frames) {
            s->animation_frame = 0;
            s->animation_enabled = o->animation == 1;
        }
        if (--s->charge_ticks == 0) {
            if (s->charge < o->power) s->charge++;
            s->charge_ticks = 20;
        }
    }
}

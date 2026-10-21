/* shots.c — player shot creation and movement, ported from OpenTyrian
 * shots.c (player_shot_create, player_shot_move_and_draw).
 * and the shot-versus-enemy part of tyrian2.c:JE_main.
 * JE_doSP sparkles use the portable particle simulation; Zinglon is a
 * synthetic collision beam outside the player-shot slots. */
#include "shots.h"
#include "game.h"
#include "player.h"
#include "enemy.h"
#include "level.h"
#include "special.h"
#include "particles.h"
#include "gen_data/items.h"
#include "gen_data/shot_shapes.h"

PlayerShot player_shots[PLAYER_SHOT_MAX];

void shots_init(void)
{
    int i;
    for (i = 0; i < PLAYER_SHOT_MAX; i++) player_shots[i].avail = 0;
}

static int16_t iabs16(int16_t v) { return v < 0 ? -v : v; }

int shot_create(uint16_t port, uint8_t bay, int16_t px, int16_t py, uint16_t wpn, uint8_t player_num)
{
    Player *pl = &players[player_num - 1];
    const Weapon *w;
    int shot_id = PLAYER_SHOT_MAX, multi;

    if (port > PORT_NUM || wpn == 0 || wpn > WEAP_NUM) return PLAYER_SHOT_MAX;
    w = &weapons[wpn];
    if (game.power < weapon_ports[port].poweruse) return PLAYER_SHOT_MAX;
    game.power -= weapon_ports[port].poweruse;
    {
        static const uint8_t sound_channel[11] = { 0, 2, 4, 4, 2, 2, 5, 5, 1, 4, 1 };
        if (w->sound > 0) game.sound_queue[sound_channel[bay < 11 ? bay : 0]] = w->sound;
    }

    for (multi = 1; multi <= w->multi; multi++) {
        PlayerShot *s;
        uint8_t mp, del;
        int16_t t;

        for (shot_id = 0; shot_id < PLAYER_SHOT_MAX; shot_id++)
            if (player_shots[shot_id].avail == 0) break;
        if (shot_id == PLAYER_SHOT_MAX) return PLAYER_SHOT_MAX;

        if (pl->shot_multi_pos[bay] == w->max || pl->shot_multi_pos[bay] > 8)
            pl->shot_multi_pos[bay] = 1;
        else
            pl->shot_multi_pos[bay]++;
        mp = pl->shot_multi_pos[bay] - 1;

        s = &player_shots[shot_id];
        s->chain = 0;
        s->player = player_num;
        s->ani = 0;
        s->complicated = w->circlesize != 0;
        if (w->circlesize == 0) {
            s->dev_x = s->dir_x = s->dev_y = s->dir_y = 0;
            s->cir_x = s->cir_y = 0;
        } else {
            uint8_t cs = w->circlesize;
            if (cs > 19) {
                s->cir_x = cs % 20;
                s->dev_x = s->cir_x >> 1;
                s->cir_y = cs / 20;
                s->dev_y = s->cir_y >> 1;
            } else {
                s->cir_x = s->cir_y = cs;
                s->dev_x = s->dev_y = cs >> 1;
            }
            s->dir_x = 1;
            s->dir_y = -1;
        }
        s->trail = w->trail;
        if (w->attack[mp] > 99 && w->attack[mp] < 250) {
            s->chain = w->attack[mp] - 100;
            s->dmg = 1;
        } else {
            s->dmg = w->attack[mp];
        }
        s->blast_filter = w->shipblastfilter;

        s->x = px + w->bx[mp];
        s->y = py + w->by[mp];
        s->yc = -w->acceleration;
        s->xc = w->accelerationx;
        s->xm = w->sx[mp];

        del = w->del[mp];
        if (del == 121) {
            s->trail = 0;
            del = 255;
        }
        s->gr = w->sg[mp];
        s->avail = s->gr == 0 ? 0 : del;
        s->ani_max = (del > 100 && del < 120) ? del - 100 + 1 : w->weapani + 1;

        /* del 98/99/100 aim with the mouse: mouseX/Y == ship position on a
         * pad, so the clamped terms are 0 (PX - mouseX) / -sy. */
        if (del == 99 || del == 100) {
            t = -w->sy[mp];
            if (t < -4) t = -4;
            else if (t > 4) t = 4;
            s->ym = t;
        } else if (w->sy[mp] == 98) {
            s->ym = 0;
            s->yc = -1;
        } else if (w->sy[mp] > 100) {   /* never: sy is s8 (as on PC, JE_shortint) */
            s->ym = w->sy[mp];
            s->y -= pl->delta_y_shot_move;
        } else {
            s->ym = -w->sy[mp];
        }
        if (w->sx[mp] > 100) {          /* never: s8 */
            s->xm = w->sx[mp];
            s->x -= pl->delta_x_shot_move;
            if (s->xm == 101) s->y -= pl->delta_y_shot_move;
        }

        s->aim_at = 0;
        if (w->aim > 5) {   /* guided: closest enemy that is not a score item */
            uint16_t best = 65000;
            int e;
            for (e = 0; e < ENEMY_MAX; e++) {
                if (enemy_avail[e] != EA_FREE && !enemies[e].scoreitem) {
                    uint16_t dd = (uint16_t)(iabs16(enemies[e].ex - s->x) + iabs16(enemies[e].ey - s->y));
                    if (dd < best) {
                        best = dd;
                        s->aim_at = (uint8_t)(e + 1);
                    }
                }
            }
            s->aim_delay = 5;
            s->aim_delay_max = w->aim - 5;
        }
        s->px = s->x;
        s->py = s->y;
        s->frame = 0;   /* first drawn after its first move, as on PC */
        pl->shot_repeat[bay] = w->shotrepeat;
    }
    return shot_id;
}

/* Enemy b dies, with everything linked to it (the "else" branch of the
 * PC's armor test): spawns, cash, flags, explosions. */
static void kill_linked(int b, uint8_t link, uint8_t player_num)
{
    int t2;
    if (link == 254 && game.super_enemy_254_jump > 0) level_event_jump(game.super_enemy_254_jump);
    for (t2 = 0; t2 < ENEMY_MAX; t2++) {
        Enemy *e = &enemies[t2];
        uint8_t l3;
        int16_t sx;
        if (enemy_avail[t2] == EA_FREE) continue;
        l3 = e->linknum;
        if (!((t2 == b) || (link == 254) ||
              ((link != 255) && ((link == l3) || (link - 100 == l3) ||
                                 ((l3 > 40) && (l3 / 20 == link / 20) && (l3 <= link))))))
            continue;
        sx = e->ex + e->mapoffset;
        if (e->special && e->flagnum >= 1 && e->flagnum <= 10) game.global_flags[e->flagnum - 1] = e->setto;
        if (e->enemydie > 0) {
            uint16_t die = e->enemydie;
            int off = t2 - (t2 % 25), nb;
            if (enemy_dat[die].value > 30000) off = 0;
            nb = enemy_new(off, die, 0);
            if (nb) {
                Enemy *n = &enemies[nb - 1];
                n->scoreitem = n->evalue != 0;
                n->ex = e->ex;
                n->ey = e->ey;
                n->px = n->ex + n->mapoffset;
                n->py = n->ey;
            }
        }
        if (e->evalue > 0 && e->evalue < 10000) {
            if (e->evalue == 1) game.cube_max++;
            else players[player_num - 1].cash += e->evalue;
        }
        if (e->edlevel == -1 && link == l3) {
            e->edlevel = 0;
            enemy_avail[t2] = EA_ITEM;
            e->egr[0] = e->edgr;
            e->ani = 1;
            e->aniactive = 0;
            e->animax = 0;
            e->animin = 1;
            e->edamaged = 1;
            e->enemycycle = 1;
        } else {
            enemy_avail[t2] = EA_FREE;
            game.enemy_killed++;
        }
        if (enemy_dat[e->enemytype].esize == 1) {
            explosion_setup_large(e->enemyground, e->explonum, sx, e->ey);
            game.sound_queue[6] = S_EXPLOSION_9;
        } else {
            explosion_setup(sx, e->ey, 0, 1, 0, 0);
            game.sound_queue[6] = S_EXPLOSION_8;
        }
    }
}

/* Damaged-state switch (edlevel / edani) of the enemies linked to b. */
static void damage_linked(int b, uint8_t link)
{
    int t3;
    for (t3 = 0; t3 < ENEMY_MAX; t3++) {
        Enemy *e = &enemies[t3];
        uint8_t ln;
        int16_t x, y;
        if (enemy_avail[t3] == EA_FREE) continue;
        ln = e->linknum;
        if (!((t3 == b) ||
              ((link != 255) &&
               (((e->edlevel > 0) && (ln == link)) ||
                ((game.enemy_continual_damage && (link - 100 == ln)) ||
                 ((ln > 40) && (ln / 20 == link / 20) && (ln <= link)))))))
            continue;
        e->enemycycle = 1;
        e->edamaged = !e->edamaged;
        if (e->edani != 0) {
            e->ani = (uint8_t)iabs16(e->edani);
            e->aniactive = 1;
            e->animax = 0;
            e->animin = (uint8_t)e->edgr;
            e->enemycycle = e->animin - 1;
        } else if (e->edgr > 0) {
            e->egr[0] = e->edgr;
            e->ani = 1;
            e->aniactive = 0;
            e->animax = 0;
            e->animin = 1;
        } else {
            enemy_avail[t3] = EA_FREE;
            game.enemy_killed++;
        }
        e->aniwhenfire = 0;
        if (e->armorleft > (uint8_t)e->edlevel) e->armorleft = (uint8_t)e->edlevel;
        x = e->ex + e->mapoffset;
        y = e->ey;
        if (enemy_dat[e->enemytype].esize != 1)
            explosion_setup(x, y - 6, 0, 1, 0, 0);
        else
            explosion_setup_large(e->enemyground, e->explonum / 2, x, y);
    }
}

/* Collisions of shot s (already moved) with the enemies; returns 0 when
 * the shot is consumed. */
/* Shootable enemies in index order, built once per tick: scanning all 100
 * slots per shot dominated the frame.  A spawn during the shot loop (kill
 * drops) invalidates it; the scan then falls back to all slots after the
 * current one, which keeps the PC's order exactly. */
static uint8_t active[ENEMY_MAX];
static int16_t active_x[ENEMY_MAX];   /* ex + mapoffset: enemies do not move during the shot loop */
static uint8_t active_n;
static uint16_t active_spawns;

static void build_active(void)
{
    int i;
    const Enemy *e = enemies;
    active_n = 0;
    for (i = 0; i < ENEMY_MAX; i++, e++) {
        if (ENEMY_QUAD_FREE(i)) {
            i += 3;
            e += 3;
            continue;
        }
        if (enemy_avail[i] == EA_ACTIVE) {
            active_x[active_n] = e->ex + e->mapoffset;
            active[active_n++] = (uint8_t)i;
        }
    }
    active_spawns = enemy_spawns;
}

static int shot_hits(PlayerShot *s, uint8_t beam)
{
    int k, b, full = 0;
    int16_t radius_x = 0, radius_y = 0, center_x = s->x;
    uint16_t broad_width, broad_offset;
    if (s->frame > 60000 && s->frame - 60001 < OPTION_SHAPE_COUNT) {
        uint16_t shape = s->frame - 60001;
        radius_x = option_shape_width[shape] >> 1;
        radius_y = option_shape_height[shape] >> 1;
        center_x += radius_x;
    }
    broad_width = 49 + radius_x * 2;
    broad_offset = 24 + radius_x;
    if (active_spawns != enemy_spawns) build_active();
    for (k = 0; full ? b + 1 < ENEMY_MAX : k < active_n; k++) {
        Enemy *e;
        int16_t dmg, armorleft;
        uint8_t link, infinite = 0, do_iced = 0, filter = s->blast_filter;
        int16_t dx;
        if (!beam && !full && (uint16_t)(active_x[k] - center_x + broad_offset) >= broad_width) continue;
        b = full ? b + 1 : active[k];
        if (enemy_avail[b] != EA_ACTIVE) continue;
        e = &enemies[b];
        dx = iabs16(e->ex + e->mapoffset - center_x);
        if (beam) {
            int16_t width = 25 - iabs16(special_state.zinglon - 25);
            if (dx >= width) continue;
        } else if (!((e->enemycycle == 0 && dx < 25 + radius_x && iabs16(e->ey - s->y - 12 - radius_y) < 29 + radius_y) ||
                     (e->enemycycle > 0 && dx < 13 + radius_x && iabs16(e->ey - s->y - 6 - radius_y) < 15 + radius_y))) {
            continue;
        }
        if (s->chain > 0) {
            players[s->player - 1].shot_multi_pos[SHOT_MISC] = 0;
            shot_create(0, SHOT_MISC, s->x, s->y, s->chain, s->player);
            return 0;
        }
        dmg = s->dmg;
        if (dmg == 99) {
            dmg = 0;
            do_iced = 40;
            e->iced = 40;
        } else if (dmg >= 250) {
            dmg -= 250;
            infinite = 1;
        }
        armorleft = e->armorleft;
        link = e->linknum ? e->linknum : 255;
        if (e->armorleft < 255) {
            int k;
            if (e->enemyground) e->filter = filter;
            for (k = 0; k < ENEMY_MAX; k++) {
                if (enemies[k].linknum == link && enemy_avail[k] != EA_FREE && enemies[k].enemyground) {
                    if (do_iced) enemies[k].iced = do_iced;
                    enemies[k].filter = filter;
                }
            }
        }
        {
            uint16_t sp = enemy_spawns;
        if (armorleft > dmg) {
            if (!beam) {
                if (e->armorleft != 255) {
                    e->armorleft -= (uint8_t)dmg;
                    explosion_setup(s->x, s->y, 0, 0, 0, 0);
                } else particles_spawn(s->x + 6, s->y + 6, dmg / 2 + 3, dmg / 4 + 2, filter);
            }
            game.sound_queue[5] = S_ENEMY_HIT;
            if ((armorleft - dmg <= e->edlevel) && ((!e->edamaged) ^ (e->edani < 0)))
                damage_linked(b, link);
        } else {
            kill_linked(b, link, s->player);
        }
            if (sp != enemy_spawns) {   /* new enemies: rebuild for later shots, scan the rest */
                build_active();
                full = 1;
            }
        }
        if (infinite) {
            dmg += 250;
        } else if (!beam) {
            if (dmg <= armorleft) return 0;
            s->dmg -= armorleft;
        }
    }
    return 1;
}

void shots_move(void)
{
    PlayerShot *s;
    build_active();
    for (s = player_shots; s < player_shots + PLAYER_SHOT_MAX; s++) {
        Player *pl;
        if (!s->avail) continue;
        pl = &players[s->player - 1];
        s->px = s->x;
        s->py = s->y;
        s->avail--;

        s->xm += s->xc;
        s->x += s->xm;
        if (s->xm > 100) {
            if (s->xm == 101) {
                s->x -= 101;
                s->x += pl->delta_x_shot_move;
                s->y += pl->delta_y_shot_move;
            } else {
                s->x -= 120;
                s->x += pl->delta_x_shot_move;
            }
        }
        s->ym += s->yc;
        s->y += s->ym;
        if (s->ym > 100) {
            s->y -= 120;
            s->y += pl->delta_y_shot_move;
        }
        if (s->complicated) {
            s->dev_x += s->dir_x;
            s->x += s->dev_x;
            if (iabs16(s->dev_x) == s->cir_x) s->dir_x = -s->dir_x;
            s->dev_y += s->dir_y;
            s->y += s->dev_y;
            if (iabs16(s->dev_y) == s->cir_y) s->dir_y = -s->dir_y;
        }
        if (s->x < -34 || s->x > 290 || s->y < -15 || s->y > 190) {
            s->avail = 0;
            continue;
        }
        if (s->trail != 255) {
            if (s->trail == 98)
                explosion_setup(s->x - s->xm, s->y - s->ym, 0, s->trail, 0, 0);
            else
                explosion_setup(s->x, s->y, 0, s->trail, 0, 0);
        }
        if (s->aim_at != 0 && --s->aim_delay == 0) {
            Enemy *t = &enemies[s->aim_at - 1];
            s->aim_delay = s->aim_delay_max;
            if (enemy_avail[s->aim_at - 1] != EA_FREE) {
                s->xm += s->x < t->ex ? 1 : -1;
                s->ym += s->y < t->ey ? 1 : -1;
            } else {
                s->xm += s->xm > 0 ? 1 : -1;
            }
        }
        /* Drawn frame and superpixels before advancing animation, as on PC. */
        s->frame = s->gr + s->ani;
        if (s->frame > 1000 && s->frame <= 60000) {
            uint8_t color;
            s->frame = particle_frame(s->frame, &color);
            particles_spawn(s->x + 7, s->y + 6, 5, 3, color);
        }
        if (++s->ani == s->ani_max) s->ani = 0;
        if (!shot_hits(s, 0)) s->avail = 0;
    }
    if (special_state.pulse) {
        PlayerShot beam = {0};
        beam.x = players[0].x + 7; beam.dmg = 10; beam.player = 1;
        beam.blast_filter = 9;
        shot_hits(&beam, 1);
        special_state.pulse = 0;
    }

}


/* enemy.c — enemies, enemy shots and explosions, ported from OpenTyrian
 * tyrian2.c (JE_drawEnemy, JE_newEnemy, JE_makeEnemy, the enemy shot and
 * explosion loops of JE_main) and varz.c (JE_setupExplosion*).
 * Single player, no galaga/arcade modes.  Not ported: sounds, the
 * enemy-sprite filter effects (stored in `filter` but not drawn). */
#include "enemy.h"
#include "game.h"
#include "player.h"
#include "rng.h"
#include "mode.h"
#include "gen_data/items.h"
#include "gen_data/difficulty.h"

Enemy enemies[ENEMY_MAX];
uint8_t enemy_avail[ENEMY_MAX] __attribute__((aligned(4)));
EnemyShot enemy_shots[ENEMY_SHOT_MAX];
Explosion explosions[EXPLOSION_MAX];
RepExplosion rep_explosions[REP_EXPLOSION_MAX];
uint8_t enemy_bank_ids[4];
uint16_t enemy_spawns;

static int16_t iabs(int16_t v) { return v < 0 ? -v : v; }
static void explo_reset(void);

/* roundf((float)num / den * mul) for |num|,|den| < 300, mul < 64: round
 * half away from zero in 16-bit integer math (exact). */
static int16_t round_ratio(int16_t num, int16_t den, int16_t mul)
{
    int16_t n = num * mul * 2;
    return (int16_t)((n + (n < 0 ? -den : den)) / (2 * den));
}

void enemies_init(void)
{
    int i;
    for (i = 0; i < ENEMY_MAX; i++) {
        enemy_avail[i] = EA_FREE;
        enemies[i].shapebank = 0;
        enemies[i].linknum = 0;
    }
    for (i = 0; i < ENEMY_SHOT_MAX; i++) enemy_shots[i].avail = 1;
    for (i = 0; i < EXPLOSION_MAX; i++) explosions[i].ttl = 0;
    explo_reset();
    for (i = 0; i < REP_EXPLOSION_MAX; i++) rep_explosions[i].ttl = 0;
    for (i = 0; i < 4; i++) enemy_bank_ids[i] = 0;
}

/* enemyDat field access with the events 49-52 override of record 0 */
static uint8_t edat_armor(uint16_t i) { return i == 0 ? game.custom_armor : enemy_dat[i].armor; }
static uint16_t edat_egr(uint16_t i, int k) { return (i == 0 && k == 0) ? game.custom_egr : enemy_dat[i].egraphic[k]; }

uint8_t enemy_make(Enemy *e, uint16_t edat, int16_t unique_bank)
{
    const EnemyDat *d = &enemy_dat[edat];
    uint8_t bank = unique_bank > 0 ? (uint8_t)unique_bank : d->shapebank, avail;
    int i;

    /* A bank that is not loaded keeps the previous occupant's sprites
     * (APPROACH relies on it), so only replace when available. */
    if (bank == 21 || bank == 26 || bank == enemy_bank_ids[0] || bank == enemy_bank_ids[1] ||
        bank == enemy_bank_ids[2] || bank == enemy_bank_ids[3])
        e->shapebank = bank;

    e->mapoffset = 0;
    for (i = 0; i < 3; i++) e->eshotmultipos[i] = 0;
    e->enemyground = (d->explosiontype & 1) == 0;
    e->explonum = d->explosiontype >> 1;
    e->launchfreq = d->elaunchfreq;
    e->launchwait = d->elaunchfreq;
    e->launchtype = d->elaunchtype % 1000;
    e->launchspecial = d->elaunchtype / 1000;
    e->xaccel = d->xaccel;
    e->yaccel = d->yaccel;
    e->xminbounce = -10000;
    e->xmaxbounce = 10000;
    e->yminbounce = -10000;
    e->ymaxbounce = 10000;
    for (i = 0; i < 3; i++) e->tur[i] = d->tur[i];
    e->ani = d->ani;
    e->animin = 1;
    switch (d->animate) {
    case 0: e->enemycycle = 1; e->aniactive = 0; e->animax = 0; e->aniwhenfire = 0; break;
    case 1: e->enemycycle = 0; e->aniactive = 1; e->animax = 0; e->aniwhenfire = 0; break;
    case 2: e->enemycycle = 1; e->aniactive = 2; e->animax = e->ani; e->aniwhenfire = 2; break;
    }
    if (d->startxc != 0)
        e->ex = d->startx + rng_mod((uint16_t)(d->startxc * 2)) - d->startxc + 1;
    else
        e->ex = d->startx + 1;
    if (d->startyc != 0)
        e->ey = d->starty + rng_mod((uint16_t)(d->startyc * 2)) - d->startyc + 1;
    else
        e->ey = d->starty + 1;
    e->exc = d->xmove;
    e->eyc = d->ymove;
    e->excc = d->xcaccel;
    e->eycc = d->ycaccel;
    e->exccw = (int8_t)iabs(e->excc);
    e->exccwmax = (uint8_t)e->exccw;
    e->eyccw = (int8_t)iabs(e->eycc);
    e->eyccwmax = (uint8_t)e->eyccw;
    e->exccadd = e->excc > 0 ? 1 : -1;
    e->eyccadd = e->eycc > 0 ? 1 : -1;
    e->special = 0;
    e->iced = 0;
    e->exrev = d->xrev == 0 ? 100 : (d->xrev == -99 ? 0 : d->xrev);
    e->eyrev = d->yrev == 0 ? 100 : (d->yrev == -99 ? 0 : d->yrev);
    e->enemytype = edat;
    for (i = 0; i < 3; i++)
        e->eshotwait[i] = e->tur[i] == 252 ? 1 : (e->tur[i] > 0 ? 20 : 255);
    for (i = 0; i < 20; i++) e->egr[i] = edat_egr(edat, i);
    e->size = d->esize;
    e->linknum = 0;
    e->edamaged = d->dani < 0;
    e->enemydie = d->eenemydie;
    for (i = 0; i < 3; i++) e->freq[i] = d->freq[i];
    e->edani = d->dani;
    e->edgr = d->dgr;
    e->edlevel = d->dlevel;
    e->fixedmovey = 0;
    e->filter = 0;
    /* Float multipliers from JE_makeEnemy are precomputed at build time. */
    e->evalue = enemy_value_scale[(uint16_t)(game.difficulty + 1) * (ENEMY_NUM + 1) + edat];
    if (edat_armor(edat) > 0) {
        e->armorleft = enemy_armor_scale[((uint16_t)(game.difficulty + 1) << 8) | edat_armor(edat)];
        avail = EA_ACTIVE;
        e->scoreitem = 0;
    } else {
        avail = EA_ITEM;
        e->armorleft = 255;
        e->scoreitem = e->evalue != 0;
    }
    if (!e->scoreitem) game.total_enemy++;
    e->moved = 0;
    enemy_spawns++;
    return avail;
}

int enemy_new(int offset, uint16_t edat, int16_t unique_bank)
{
    int i;
    for (i = offset; i < offset + 25; i++) {
        if (enemy_avail[i] == EA_FREE) {
            enemy_avail[i] = enemy_make(&enemies[i], edat, unique_bank);
            return i + 1;
        }
    }
    return 0;
}

static void fire(Enemy *e, int j, uint8_t t, int16_t tx, int16_t ty, int16_t map_x_ofs, int16_t player_x)
{
    const Weapon *w = &weapons[t];
    int count;

    for (count = w->multi; count > 0; count--) {
        EnemyShot *s;
        int b, pos;
        for (b = 0; b < ENEMY_SHOT_MAX; b++)
            if (enemy_shots[b].avail == 1) break;
        if (b == ENEMY_SHOT_MAX) return;
        s = &enemy_shots[b];
        s->avail = 0;
        /* the PC picks a random channel (not 3) */
        if (w->sound > 0) game.sound_queue[1] = w->sound;
        if (e->aniactive == 2) e->aniactive = 1;
        if (++e->eshotmultipos[j - 1] > w->max) e->eshotmultipos[j - 1] = 1;
        pos = e->eshotmultipos[j - 1] - 1;

        s->sx = tx + w->bx[pos] + map_x_ofs;
        s->sy = ty + w->by[pos];
        s->sdmg = w->attack[pos];
        s->tx = w->tx;
        s->ty = w->ty;
        s->duration = w->del[pos];
        s->animate = 0;
        s->animax = w->weapani;
        s->sgr = w->sg[pos];
        switch (j) {
        case 1:
            s->syc = w->acceleration;
            s->sxc = w->accelerationx;
            s->sxm = w->sx[pos];
            s->sym = w->sy[pos];
            break;
        case 3:
            s->sxc = -w->acceleration;
            s->syc = w->accelerationx;
            s->sxm = -w->sy[pos];
            s->sym = -w->sx[pos];
            break;
        case 2:
            s->sxc = w->acceleration;
            s->syc = -w->acceleration;
            s->sxm = w->sy[pos];
            s->sym = -w->sx[pos];
            break;
        }
        if (w->aim > 0) {
            int16_t aim = w->aim;
            int16_t ax = (player_x + 25) - tx - map_x_ofs - 4;
            int16_t ay = players[0].y - ty;
            int16_t mag;
            if (game.difficulty > DIFFICULTY_NORMAL) aim += game.difficulty - 2;
            if (ax == 0) ax = 1;
            if (ay == 0) ay = 1;
            mag = iabs(ax) > iabs(ay) ? iabs(ax) : iabs(ay);
            s->sxm = round_ratio(ax, mag, aim);
            s->sym = round_ratio(ay, mag, aim);
        }
        s->px = s->sx;
        s->py = s->sy;
    }
}

void enemies_update(int offset, int16_t map_x_ofs, int16_t back_move)
{
    /* the PC shifts player[0].x by -25 for the whole routine */
    int16_t player_x = players[0].x - 25;
    int i;
    Enemy *e;

    for (i = offset - 25, e = &enemies[offset - 25]; i < offset; i++, e++) {
        int16_t tx, ty;
        int j;

        if (i + 4 <= offset && ENEMY_QUAD_FREE(i)) {
            i += 3;
            e += 3;
            continue;
        }
        if (enemy_avail[i] == EA_FREE) continue;
        e->mapoffset = map_x_ofs;
        e->px = e->ex + map_x_ofs;
        e->py = e->ey;
        e->moved = 1;

        if (e->xaccel && (uint16_t)(e->xaccel - 89) > rng_mod(11)) {
            if (player_x > e->ex) {
                if (e->exc < e->xaccel - 89) e->exc++;
            } else if (e->exc >= 0 || -e->exc < e->xaccel - 89) {
                e->exc--;
            }
        }
        if (e->yaccel && (uint16_t)(e->yaccel - 89) > rng_mod(11)) {
            if (players[0].y > e->ey) {
                if (e->eyc < e->yaccel - 89) e->eyc++;
            } else if (e->eyc >= 0 || -e->eyc < e->yaccel - 89) {
                e->eyc--;
            }
        }

        if (e->ex + map_x_ofs > -29 && e->ex + map_x_ofs < 300) {
            if (e->aniactive == 1) {
                e->enemycycle++;
                if (e->enemycycle == e->animax)
                    e->aniactive = e->aniwhenfire;
                else if (e->enemycycle > e->ani)
                    e->enemycycle = e->animin;
            }
            if (e->enemycycle >= 1 && e->enemycycle <= 20 && e->egr[e->enemycycle - 1] == 999)
                goto enemy_gone;
            e->filter = 0;
        }

        if (e->excc) {
            if (--e->exccw <= 0) {
                if (e->exc == e->exrev) {
                    e->excc = -e->excc;
                    e->exrev = -e->exrev;
                    e->exccadd = -e->exccadd;
                } else {
                    e->exc += e->exccadd;
                    e->exccw = (int8_t)e->exccwmax;
                    if (e->exc == e->exrev) {
                        e->excc = -e->excc;
                        e->exrev = -e->exrev;
                        e->exccadd = -e->exccadd;
                    }
                }
            }
        }
        if (e->eycc) {
            if (--e->eyccw <= 0) {
                if (e->eyc == e->eyrev) {
                    e->eycc = -e->eycc;
                    e->eyrev = -e->eyrev;
                    e->eyccadd = -e->eyccadd;
                } else {
                    e->eyc += e->eyccadd;
                    e->eyccw = (int8_t)e->eyccwmax;
                    if (e->eyc == e->eyrev) {
                        e->eycc = -e->eycc;
                        e->eyrev = -e->eyrev;
                        e->eyccadd = -e->eyccadd;
                    }
                }
            }
        }

        e->ey += e->fixedmovey;
        e->ex += e->exc;
        if (e->ex < -80 || e->ex > 340) goto enemy_gone;
        e->ey += e->eyc;
        if (e->ey < -112 || e->ey > 190) goto enemy_gone;

        if (e->ex <= e->xminbounce || e->ex >= e->xmaxbounce) e->exc = -e->exc;
        if (e->ey <= e->yminbounce || e->ey >= e->ymaxbounce) e->eyc = -e->eyc;
        if (e->scoreitem) {
            if (e->ex < -5) e->ex++;
            if (e->ex > 245) e->ex--;
        }
        e->ey += back_move;

        if (e->ex <= -24 || e->ex >= 296) continue;
        tx = e->ex;
        ty = e->ey;

        if (e->edamaged) continue;
        game.enemy_on_screen++;
        if (e->iced) {
            e->iced--;
            if (e->enemyground) e->filter = 0x09;
            continue;
        }

        for (j = 3; j > 0; j--) {
            uint8_t t;
            if (!e->freq[j - 1]) continue;
            t = e->tur[j - 1];
            if (--e->eshotwait[j - 1] == 0 && t) {
                e->eshotwait[j - 1] = e->freq[j - 1];
                if (game.difficulty > DIFFICULTY_NORMAL) {
                    e->eshotwait[j - 1] = e->eshotwait[j - 1] / 2 + 1;
                    if (game.difficulty > DIFFICULTY_MANIACAL)
                        e->eshotwait[j - 1] = e->eshotwait[j - 1] / 2 + 1;
                }
                if (game_mode.galaga && (!e->eyc || rng_mod(400) >= game_mode.shot_frequency)) goto enemy_end;
                switch (t) {
                case 252:   /* Savara boss dual missile */
                    if (e->ey > 20) {
                        explosion_setup(tx - 8 + map_x_ofs, ty - 20 - game.back_move[0] * 8, -2, 6, 0, 0);
                        explosion_setup(tx + 4 + map_x_ofs, ty - 20 - game.back_move[0] * 8, -2, 6, 0, 0);
                    }
                    break;
                case 251: { /* suck-o-magnet */
                    int16_t att = 4 - (iabs(player_x - tx) + iabs(players[0].y - ty)) / 100;
                    if (att > 0) players[0].x_velocity += player_x > tx ? -att : att;
                    break;
                }
                case 253:   /* short range magnets */
                    if (iabs(player_x + 25 - 14 - tx) < 24 && iabs(players[0].y - ty) < 28)
                        players[0].x_velocity += 2;
                    break;
                case 254:
                    if (iabs(player_x + 25 - 14 - tx) < 24 && iabs(players[0].y - ty) < 28)
                        players[0].x_velocity -= 2;
                    break;
                case 255:   /* magneto repulse */
                    if (game.difficulty != DIFFICULTY_EASY) {
                        if (j == 3) {
                            e->filter = 0x70;
                        } else {
                            int16_t rep = 4 - (iabs(player_x - tx) + iabs(players[0].y - ty)) / 20;
                            if (rep > 0) players[0].x_velocity += player_x > tx ? rep : -rep;
                        }
                    }
                    break;
                default:
                    fire(e, j, t, tx, ty, map_x_ofs, player_x);
                    break;
                }
            }
        }

        if (e->launchfreq && --e->launchwait == 0) {
            int b;
            e->launchwait = e->launchfreq;
            if (e->launchspecial != 0 && iabs(e->ey - players[0].y) > 5) continue;
            if (e->aniactive == 2) e->aniactive = 1;
            if (e->launchtype == 0) continue;
            b = enemy_new(offset == 50 ? 75 : offset - 25, e->launchtype, 0);
            if (b > 0) {
                Enemy *n = &enemies[b - 1];
                n->ex = tx;
                n->ey = ty + enemy_dat[n->enemytype].startyc;
                if (n->size == 0) n->ey -= 7;
                if (n->launchtype > 0 && n->launchfreq == 0) {
                    if (n->launchtype > 90) {
                        n->ex += rng_mod((uint16_t)((n->launchtype - 90) * 4)) - (n->launchtype - 90) * 2;
                    } else {
                        int16_t ax = (player_x + 25) - tx - map_x_ofs - 4;
                        int16_t ay = players[0].y - ty, mag;
                        if (ax == 0) ax = 1;
                        if (ay == 0) ay = 1;
                        mag = iabs(ax) > iabs(ay) ? iabs(ax) : iabs(ay);
                        n->exc = (int8_t)round_ratio(ax, mag, (int16_t)n->launchtype);
                        n->eyc = (int8_t)round_ratio(ay, mag, (int16_t)n->launchtype);
                    }
                }
                n->px = n->ex + map_x_ofs;
                n->py = n->ey;
                if (e->launchspecial == 1 && e->linknum < 100) n->linknum = e->linknum;
            }
        }
enemy_end:
        continue;
enemy_gone:
        enemy_avail[i] = EA_FREE;
    }
}

void enemy_shots_update(void)
{
    Player *p = &players[0];
    int z;
    for (z = 0; z < ENEMY_SHOT_MAX; z++) {
        EnemyShot *s = &enemy_shots[z];
        if (s->avail) continue;
        s->px = s->sx;
        s->py = s->sy;
        s->sxm += s->sxc;
        s->sx += s->sxm;
        if (s->tx != 0) {
            if (s->sx > p->x) {
                if (s->sxm > -s->tx) s->sxm--;
            } else if (s->sxm < s->tx) {
                s->sxm++;
            }
        }
        s->sym += s->syc;
        s->sy += s->sym;
        if (s->ty != 0) {
            if (s->sy > p->y) {
                if (s->sym > -s->ty) s->sym--;
            } else if (s->sym < s->ty) {
                s->sym++;
            }
        }
        if (s->duration-- == 0 || s->sy > 190 || s->sy <= -14 || s->sx > 275 || s->sx <= 0) {
            s->avail = 1;
            continue;
        }
        if (p->is_alive &&
            s->sx > p->x - p->hit_area_x && s->sx < p->x + p->hit_area_x &&
            s->sy > p->y - p->hit_area_y && s->sy < p->y + p->hit_area_y) {
            uint8_t dmg = s->sdmg;
            s->avail = 1;
            explosion_setup(s->sx, s->sy, 0, 0, 0, 0);
            if (p->invulnerable_ticks == 0 && (dmg = player_damage(dmg)) > 0) {
                p->x_velocity += (s->sxm * dmg) / 2;
                p->y_velocity += (s->sym * dmg) / 2;
            }
            continue;
        }
        if (s->animax != 0 && ++s->animate >= s->animax) s->animate = 0;
    }
}

static const struct { uint16_t sprite; uint8_t ttl; } explosion_data[53] = {
    {144, 7}, {120, 12}, {190, 12}, {209, 12}, {152, 12}, {171, 12}, {133, 7}, {1, 12}, {20, 12},
    {39, 12}, {58, 12}, {110, 3}, {76, 7}, {91, 3}, {227, 3}, {230, 3}, {233, 3}, {252, 3},
    {246, 3}, {249, 3}, {265, 3}, {268, 3}, {271, 3}, {236, 3}, {239, 3}, {242, 3}, {261, 3},
    {274, 3}, {277, 3}, {280, 3}, {299, 3}, {284, 3}, {287, 3}, {290, 3}, {293, 3}, {165, 8},
    {184, 8}, {203, 8}, {222, 8}, {168, 8}, {187, 8}, {206, 8}, {225, 10}, {169, 10}, {188, 10},
    {207, 20}, {226, 14}, {170, 14}, {189, 14}, {208, 14}, {246, 14}, {227, 14}, {265, 14}
};

/* Free explosion slots as a stack: the PC scans for the first free one
 * (200 slots); the slot used does not matter to the game (explosions are
 * visual), and the scan was a large share of shot trails' cost. */
static uint8_t explo_free[EXPLOSION_MAX];
static uint8_t explo_free_n;

static void explo_reset(void)
{
    int i;
    explo_free_n = 0;
    for (i = EXPLOSION_MAX - 1; i >= 0; i--) explo_free[explo_free_n++] = (uint8_t)i;
}

void explosion_setup(int16_t x, int16_t y, int16_t delta_y, int16_t type, uint8_t fixed, uint8_t follow)
{
    if (y <= -16 || y >= 190 || !explo_free_n) return;
    {
        Explosion *x_ = &explosions[explo_free[--explo_free_n]];
        x_->x = x;
        x_->y = y;
        if (type == 6) {
            x_->y += 12;
            x_->x += 2;
        } else if (type == 98) {
            type = 6;
        }
        if (type < 0 || type >= 53) type = 0;
        x_->sprite = explosion_data[type].sprite;
        x_->ttl = explosion_data[type].ttl;
        x_->follow_player = follow;
        x_->fixed_position = fixed;
        x_->delta_y = delta_y;
        x_->px = x_->x;
        x_->py = x_->y;
    }
}

void explosion_setup_large(uint8_t ground, uint8_t explo_num, int16_t x, int16_t y)
{
    uint8_t big;
    int i;
    if (y < 0) return;
    if (ground) {
        explosion_setup(x - 6, y - 14, 0, 2, 0, 0);
        explosion_setup(x + 6, y - 14, 0, 4, 0, 0);
        explosion_setup(x - 6, y, 0, 3, 0, 0);
        explosion_setup(x + 6, y, 0, 5, 0, 0);
    } else {
        explosion_setup(x - 6, y - 14, 0, 7, 0, 0);
        explosion_setup(x + 6, y - 14, 0, 9, 0, 0);
        explosion_setup(x - 6, y, 0, 8, 0, 0);
        explosion_setup(x + 6, y, 0, 10, 0, 0);
    }
    big = explo_num > 10;
    if (big) explo_num -= 10;
    if (!explo_num) return;
    for (i = 0; i < REP_EXPLOSION_MAX; i++) {
        RepExplosion *r = &rep_explosions[i];
        if (r->ttl != 0) continue;
        r->ttl = explo_num;
        r->delay = 2;
        r->x = x;
        r->y = y;
        r->big = big;
        return;
    }
}

void explosions_update(void)
{
    int i;
    game.enemy_still_exploding = 0;
    for (i = 0; i < REP_EXPLOSION_MAX; i++) {
        RepExplosion *r = &rep_explosions[i];
        int16_t x, y;
        if (r->ttl == 0) continue;
        game.enemy_still_exploding = 1;
        if (r->delay > 0) {
            r->delay--;
            continue;
        }
        r->y += game.back_move[1] + 1;
        x = r->x + rng_mod(24) - 12;
        y = r->y + rng_mod(27) - 24;
        if (r->big) {
            explosion_setup_large(0, 2, x, y);
            game.sound_queue[r->ttl == 1 ? 7 : 6] = r->ttl == 1 ? S_EXPLOSION_11 : S_EXPLOSION_9;
            r->delay = 4 + rng_mod(3);
        } else {
            explosion_setup(x, y, 0, 1, 0, 0);
            game.sound_queue[5] = S_EXPLOSION_4;
            r->delay = 3;
        }
        r->ttl--;
    }
    for (i = 0; i < EXPLOSION_MAX; i++) {
        Explosion *x = &explosions[i];
        if (x->ttl == 0) continue;
        x->px = x->x;
        x->py = x->y;
        if (!x->fixed_position) {
            x->sprite++;
            x->y += game.explode_move;
        }
        /* followPlayer (shield sparks) needs explosionFollowAmount: later */
        x->y += x->delta_y;
        if (x->y > 200 - 14)
            x->ttl = 0;
        else
            x->ttl--;
        if (x->ttl == 0) explo_free[explo_free_n++] = (uint8_t)i;
    }
}

/* player.c — ship movement and weapon firing, ported from OpenTyrian
 * mainint.c:JE_playerMovement (one player, pad input, no network/demo). */
#include "player.h"
#include "game.h"
#include "shots.h"
#include "enemy.h"
#include "rng.h"
#include "sidekick.h"
#include "special.h"
#include "gen_data/items.h"
#include "gen_data/level.h"

Player players[1];

#define KEY_SPEED 1   /* CURRENT_KEY_SPEED */

void player_init(void)
{
    Player *p = &players[0];
    /* JE: New Game Items/Data (mainint.c), playfield entry (tyrian2.c) */
    p->option_id[0] = p->option_id[1] = 0;
    p->orbit_phase = 0;
    p->special_id = 0;
    p->ship = 1;                 /* USP Talon */
    p->generator = 2;            /* Advanced MR-12 */
    p->weapon_id[FRONT_WEAPON] = 1;   /* Pulse Cannon */
    p->weapon_id[REAR_WEAPON] = 0;
    p->weapon_power[0] = p->weapon_power[1] = 1;
    p->weapon_mode = 1;
    p->shield_item = 4;          /* Gencore High Energy Shield */
    /* tyrian2.c:newGame, single-player story starting funds. */
    static const uint16_t initial_cash[4] = {10000, 15000, 20000, 30000};
    p->cash = initial_cash[LEVEL_EPISODE - 1];
}

/* tyrian2.c level start: position, shields, armor, invulnerability */
void player_start_level(void)
{
    Player *p = &players[0];
    p->x = p->px = 100;
    p->y = p->py = 180;
    p->x_velocity = p->y_velocity = 0;
    p->x_friction_ticks = p->y_friction_ticks = 0;
    p->last_x_shot_move = p->x;
    p->last_y_shot_move = p->y;
    p->delta_x_shot_move = p->delta_y_shot_move = 0;
    p->banking = 0;
    {
        int i;
        for (i = 0; i < SHOT_BAYS; i++) {
            p->shot_repeat[i] = 1;      /* memset(shotRepeat, 1) */
            p->shot_multi_pos[i] = 0;
        }
    }
    p->armor = p->initial_armor = ships[p->ship].dmg;
    p->shield = shields[p->shield_item].mpwr;
    p->shield_max = p->shield * 2;
    if (ships[p->ship].ani == 0) {
        p->hit_area_x = 12;
        p->hit_area_y = 10;
    } else {
        p->hit_area_x = 11;
        p->hit_area_y = 14;
    }
    p->invulnerable_ticks = 100;
    p->is_alive = 1;
    p->exploding_ticks = 0;
    p->mode_button_held = 0;
    p->superbombs = 0;
    sidekicks_init();
    special_init();
}

static int16_t iabs(int16_t v) { return v < 0 ? -v : v; }

static int16_t clamp(int16_t v, int16_t lo, int16_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* JE_mainGamePlayerFunctions' parallax: map offsets from the ship X. */
static void update_map_offsets(void)
{
    int16_t t = (int16_t)(((296 - players[0].x) * 72) / 224) - 1;
    game.map_x_ofs[2] = t;
    game.map_x_ofs[1] = (t * 2) / 3;
    game.old_map_x_ofs = game.map_x_ofs[0];
    game.map_x_ofs[0] = game.map_x_ofs[1] / 2;
    if (game.background3x1) game.map_x_ofs[2] = game.map_x_ofs[0];
}

void player_tick(uint16_t pad)
{
    Player *p = &players[0];
    int16_t mouse_x = p->x, mouse_y = p->y;   /* position at tick start */
    int16_t accel_x, accel_y, bank;
    int bay;

    p->px = p->x;
    p->py = p->y;
    sidekicks_before_move();

    if (game.end_level && game.level_end > 0) {
        game.level_end--;
        game.level_end_warp++;
    }
    if (!p->is_alive) {
        if (p->exploding_ticks > 0) {
            p->exploding_ticks--;
            explosion_setup_large(0, 0, p->x + rng_mod(32) - 16, p->y + rng_mod(32) - 16 + 7);
            explosion_setup_large(0, 0, p->x, p->y + 7);
            if (game.level_end > 0) game.level_end--;
        }
        if (!game.end_level) sidekicks_animate();
        return;
    }
    if (game.end_level) {
        /* end-of-level warp: the ship flies off the top */
        p->y -= game.level_end_warp;
        if (game.level_end == 0 || p->y < -200) game.really_end_level = 1;
        update_map_offsets();
        return;
    }

    if (pad & PAD_UP)    p->y -= KEY_SPEED;
    if (pad & PAD_DOWN)  p->y += KEY_SPEED;
    if (pad & PAD_LEFT)  p->x -= KEY_SPEED;
    if (pad & PAD_RIGHT) p->x += KEY_SPEED;
    accel_x = p->x - mouse_x;
    accel_y = p->y - mouse_y;

    if (p->x_friction_ticks > 0) {
        p->x_friction_ticks--;
    } else {
        p->x_friction_ticks = 1;
        if (p->x_velocity < 0) p->x_velocity++;
        else if (p->x_velocity > 0) p->x_velocity--;
    }
    if (p->y_friction_ticks > 0) {
        p->y_friction_ticks--;
    } else {
        p->y_friction_ticks = 2;
        if (p->y_velocity < 0) p->y_velocity++;
        else if (p->y_velocity > 0) p->y_velocity--;
    }
    p->x_velocity = clamp(p->x_velocity + accel_x, -4, 4);
    p->y_velocity = clamp(p->y_velocity + accel_y, -4, 4);
    p->x += p->x_velocity;
    p->y += p->y_velocity;
    sidekicks_record_move(mouse_x, mouse_y);

    p->x = clamp(p->x, 40, 256);
    p->y = clamp(p->y, 10, 160);

    bank = p->x_velocity / 2 + (p->x - mouse_x) / 6;
    p->banking = (int8_t)clamp(bank, -2, 2);
    if (p->invulnerable_ticks > 0) p->invulnerable_ticks--;

    p->delta_x_shot_move = p->x - p->last_x_shot_move;
    p->delta_y_shot_move = p->y - p->last_y_shot_move;

    /* change rear weapon mode: button[3] is an edge (action_pressed) */
    if (pad & PAD_C) {
        if (!p->mode_button_held) {
            p->shot_multi_pos[REAR_WEAPON] = 0;
            if (++p->weapon_mode > weapon_ports[p->weapon_id[REAR_WEAPON]].opnum) p->weapon_mode = 1;
            p->mode_button_held = 1;
        }
    } else {
        p->mode_button_held = 0;
    }


    special_tick(pad);
    for (bay = 0; bay < 2; bay++) {
        uint16_t item = p->weapon_id[bay];
        if (!item) continue;
        if (p->shot_repeat[bay] > 0) {
            p->shot_repeat[bay]--;
        } else if (pad & PAD_B) {
            uint8_t mode = bay == REAR_WEAPON ? p->weapon_mode - 1 : 0;
            shot_create(item, (uint8_t)bay, p->x, p->y,
                        weapon_ports[item].op[mode][p->weapon_power[bay] - 1], 1);
        }
    }
    /* JE_playerMovement: bomb cooldown runs only while bombs are held.
     * The PC sidekick buttons also release bombs; A/Z retain that pairing. */
    if (p->superbombs) {
        if (p->shot_repeat[SHOT_SUPERBOMB]) p->shot_repeat[SHOT_SUPERBOMB]--;
        else if (pad & (PAD_A | PAD_Z)) {
            p->superbombs--;
            p->shot_multi_pos[SHOT_SUPERBOMB] = 0;
            shot_create(16, SHOT_SUPERBOMB, p->x, p->y, 535, 1);
        }
    }
    sidekicks_tick(pad);
    sidekicks_animate();
    update_map_offsets();
}

uint8_t player_power_up(uint8_t port)
{
    Player *p = &players[0];
    if (p->weapon_id[port] != 0 && p->weapon_power[port] < 11) {
        p->weapon_power[port]++;
        p->shot_multi_pos[port] = 0;
        return 1;
    }
    p->cash += 1000;
    return 0;
}

uint8_t player_damage(uint8_t dmg)
{
    Player *p = &players[0];
    uint8_t taken = 0;
    game.sound_queue[7] = S_SHIELD_HIT;
    if (p->shield < dmg) {
        taken = dmg;
        dmg -= p->shield;
        p->shield = 0;
        if (dmg > 0) {
            if (p->armor < dmg) {
                p->armor = 0;
                if (p->is_alive) {
                    game.sound_queue[1] = S_EXPLOSION_22;
                    p->is_alive = 0;
                    p->exploding_ticks = 60;
                    game.level_end = 40;
                }
            } else {
                p->armor -= dmg;
                game.sound_queue[7] = S_HULL_HIT;
            }
        }
    } else {
        static const int8_t sx[9] = { -17, -5, 7, 19, -17, 19, -17, -5, 7 };
        static const int8_t sy[9] = { -12, -12, -12, -12, 2, 2, 16, 16, 16 };
        int i;
        p->shield -= dmg;
        for (i = 0; i < 9; i++) explosion_setup(p->x + sx[i], p->y + sy[i], 0, 14 + i, 0, 1);
    }
    return taken;
}

/* Kill enemy b and its link group (collision branch of JE_playerCollide). */
static void collide_kill(int b, uint8_t link)
{
    int t2;
    for (t2 = 0; t2 < ENEMY_MAX; t2++) {
        Enemy *e = &enemies[t2];
        uint8_t l3;
        if (enemy_avail[t2] == EA_FREE) continue;
        l3 = e->linknum;
        if (t2 == b || (link != 255 && (link == l3 || link - 100 == l3 ||
                                        (l3 > 40 && l3 / 20 == link / 20 && l3 <= link)))) {
            int16_t sx = e->ex + e->mapoffset;
            e->linknum = 0;
            enemy_avail[t2] = EA_FREE;
            if (enemy_dat[e->enemytype].esize == 1) {
                explosion_setup_large(e->enemyground, e->explonum, sx, e->ey);
                game.sound_queue[6] = S_EXPLOSION_9;
            } else {
                explosion_setup(sx, e->ey, 0, 1, 0, 0);
                game.sound_queue[5] = S_EXPLOSION_4;
            }
        }
    }
    enemy_avail[b] = EA_FREE;
}

void player_collide(void)
{
    Player *p = &players[0];
    int z;
    Enemy *e;
    for (z = 0, e = enemies; z < ENEMY_MAX; z++, e++) {
        int16_t sx, ev;
        if (ENEMY_QUAD_FREE(z)) {
            z += 3;
            e += 3;
            continue;
        }
        if (enemy_avail[z] == EA_FREE) continue;
        sx = e->ex + e->mapoffset;
        if (!(iabs(p->x - sx) < 12 && iabs(p->y - e->ey) < 14)) continue;
        ev = e->evalue;
        if (ev > 29999) {
            /* 30000: purple ball; 30001+: arcade/2-player weapons and items,
             * which a story game ignores (except the ball's cash) */
            if (ev == 30000) {
                p->cash += 100;
                enemy_avail[z] = EA_FREE;
            } else if (ev > 32100) {
                p->cash += 250;
                if (ev - 32100 <= SPECIAL_NUM) special_equip(ev - 32100);
                game.sound_queue[7] = S_POWERUP;
                enemy_avail[z] = EA_FREE;
            }
        } else if (ev > 20000) {
            p->armor += ev - 20000;
            if (p->armor > 28) p->armor = 28;
            enemy_avail[z] = EA_FREE;
            game.sound_queue[7] = S_POWERUP;
        } else if (ev > 10000 && enemy_avail[z] == EA_ITEM) {
            enemy_avail[z] = EA_FREE;    /* bonus level entry: level flow later */
        } else if (e->scoreitem) {
            enemy_avail[z] = EA_FREE;
            game.sound_queue[7] = S_ITEM;
            if (ev == 1) {
                game.cube_max++;
                game.sound_queue[3] = V_DATA_CUBE;
            }
            else if (ev == -1) { player_power_up(FRONT_WEAPON); game.sound_queue[7] = S_POWERUP; }
            else if (ev == -2) { player_power_up(REAR_WEAPON); game.sound_queue[7] = S_POWERUP; }
            else if (ev == -3) {
                p->shot_multi_pos[SHOT_MISC] = 0;
                shot_create(0, SHOT_MISC, p->x, p->y, 104, 1);
            } else if (ev == -3) {
                p->shot_multi_pos[SHOT_MISC] = 0;
                shot_create(0, SHOT_MISC, p->x, p->y, 104, 1);
            } else if (ev == -4) {
                if (p->superbombs < 10) p->superbombs++;
            } else if (ev == -5) {
                p->weapon_id[FRONT_WEAPON] = 25;   /* HOT DOG! */
                p->weapon_id[REAR_WEAPON] = 26;
                p->weapon_mode = 1;
            } else {
                p->cash += ev;
            }
            explosion_setup(sx, e->ey, 0, enemy_dat[e->enemytype].explosiontype, 1, 0);
        } else if (p->invulnerable_ticks == 0 && enemy_avail[z] == EA_ACTIVE &&
                   (enemy_dat[e->enemytype].explosiontype & 1) == 0) {
            int16_t armorleft = e->armorleft, armorleft2;
            uint8_t link;
            if (armorleft > game.damage_rate) armorleft = game.damage_rate;
            player_damage((uint8_t)armorleft);
            if (e->armorleft > 0) {
                p->x_velocity += (e->exc * e->armorleft) / 2;
                p->y_velocity += (e->eyc * e->armorleft) / 2;
            }
            armorleft2 = e->armorleft == 255 ? 30000 : e->armorleft;
            link = e->linknum ? e->linknum : 255;
            if (armorleft2 > armorleft) {
                if (e->armorleft != 255) e->armorleft -= (uint8_t)armorleft;
            } else {
                collide_kill(z, link);
            }
        }
    }
}

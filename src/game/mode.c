#include "mode.h"
#include "game.h"
#include "player.h"
#include "password.h"
#include "shots.h"
#include "gen_data/items.h"
#include "gen_data/motion.h"
#include "gen_data/level.h"

GameMode game_mode;
static char backup[PASSWORD_LENGTH + 1];

void mode_reset(void)
{
    game_mode = (GameMode){0};
    backup[0] = 0;
}

void mode_checkpoint(void)
{
    password_encode(backup);
}

uint8_t mode_restore(void)
{
    /* password_load decodes all bytes before game_new clears this buffer. */
    return backup[0] && password_load(backup);
}

void mode_engage(void)
{
    Player *p = &players[0];
    if (!backup[0]) mode_checkpoint();
    game_mode.arcade = game_mode.super_tyrian = game_mode.no_backup = 1;
    p->cash = 0;
    p->ship = 13;
    p->weapon_id[0] = 39; p->weapon_id[1] = 0;
    p->weapon_power[0] = 3; p->weapon_power[1] = 1;
    p->option_id[0] = p->option_id[1] = 0;
    p->generator = 2; p->shield_item = 4; p->special_id = 0;
}

void mode_galaga(void)
{
    game_mode.galaga = 1;
}

void mode_level_start(void)
{
    if (game_mode.galaga) {
        game.difficulty = DIFFICULTY_NORMAL;
        players[0].shield = 0;
    }
    game_mode.shot_frequency = 0;
    game_mode.next_life = 10000;
    game_mode.buddy = game_mode.charge = 0;
    game_mode.charge_wait = 5;
    game_mode.link_phase = 0x80000000u; /* M_PI */
    if (game_mode.galaga) {
        Player *p = &players[1];
        *p = players[0];
        p->weapon_id[REAR_WEAPON] = 15; /* copied items, then Vulcan rear */
        p->weapon_power[REAR_WEAPON] = 1;
        p->purple_balls_needed = 1;
        p->option_id[0] = p->option_id[1] = 0;
        p->x = p->px = p->last_x_shot_move = 190;
    }
}

void mode_tick(void)
{
    Player *p = &players[0];
    if (!game_mode.galaga) return;
    p->shield = players[1].shield = 0;
    if (!players[1].weapon_power[REAR_WEAPON] || !players[1].armor) game_mode.buddy = 0;
    if (p->cash >= game_mode.next_life) {
        game.sound_queue[6] = S_EXPLOSION_11;
        game.sound_queue[7] = 21; /* sndmast.h:S_SOUL_OF_ZINGLON */
        if (p->weapon_power[0] < 11) p->weapon_power[0]++;
        else p->cash += 1000;
        game_mode.next_life = game_mode.next_life == 10000 ? 20000 : game_mode.next_life + 25000;
    }
}

uint8_t mode_respawn(void)
{
    Player *p = &players[0];
    if (!game_mode.arcade || p->weapon_power[0] <= 1) return 0;
    p->weapon_power[0]--;
    if (game_mode.galaga) game_mode.buddy = 0;
    p->shot_multi_pos[0] = 0;
    p->y = 160;
    p->invulnerable_ticks = 100;
    p->is_alive = 1;
    p->armor = game_mode.galaga || LEVEL_EPISODE == 4 ? p->initial_armor : p->initial_armor / 2;
    p->shield = game_mode.galaga ? 0 : p->shield_max / 2;
    game.end_level = game.really_end_level = 0;
    return 1;
}

void mode_leave_level(void)
{
    /* JE_loadMap's new_game label; SuperTyrian and arcade persist. */
    game_mode.galaga = game_mode.extra = game_mode.no_backup = game_mode.buddy = 0;
}


void mode_collect_buddy(uint8_t player_num)
{
    Player *p = &players[1];
    players[player_num - 1].cash += game_mode.buddy ? 2500 : 100;
    game_mode.buddy = 1;
    p->weapon_power[REAR_WEAPON] = 1;
    p->armor = 10;
    p->is_alive = 1;
}

/* 0.2 radians per turn step, in an unsigned full-turn phase. Unlike a
 * float accumulator, the phase wraps exactly; 1024 Q15 angles supply trig. */
#define LINK_STEP 0x0826135Fu
static int16_t wave(uint32_t phase, int16_t radius)
{
    int32_t value = (int32_t)particle_sine[phase >> 22] * radius;
    return value < 0 ? -(int16_t)((-value + 16384) >> 15) : (int16_t)((value + 16384) >> 15);
}

static void turn_link(uint16_t pad)
{
    int16_t dx = !!(pad & PAD_RIGHT) - !!(pad & PAD_LEFT);
    int16_t dy = !!(pad & PAD_DOWN) - !!(pad & PAD_UP);
    uint32_t angle = game_mode.link_phase, target, distance;
    if (!(pad & PAD_B) || (!dx && !dy)) return;
    target = dy ? (dy > 0 ? 0 : 0x80000000u) : (dx > 0 ? 0x40000000u : 0xC0000000u);
    distance = angle > target ? angle - target : target - angle;
    if (distance < 0x0C391D0Fu) angle = target; /* fabs < 0.3 */
    else if (angle < target && target - angle < 0x84026D05u) angle += LINK_STEP; /* > -3.24 */
    else if (angle < target || angle - target < 0x80000000u) angle -= LINK_STEP;
    else angle += LINK_STEP;
    game_mode.link_phase = angle;
}

void mode_buddy_tick(uint16_t pad)
{
    Player *p = &players[1];
    uint32_t phase;
    uint16_t shot;
    int16_t mouse_x = p->x, bank;
    if (!game_mode.buddy || !p->is_alive) return;
    p->px = p->x; p->py = p->y;
    turn_link(pad);
    phase = game_mode.link_phase;
    p->x = players[0].x - (ships[p->ship].ani == 0);
    p->y = players[0].y + 8;
    p->x_velocity = players[0].x_velocity;
    p->y_velocity = 4;
    if (game.end_level) return;

    p->shot_multi_pos[SHOT_MISC] = 0;
    shot_create(0, SHOT_MISC, p->x + 1 + wave(phase + LINK_STEP, 26),
                p->y + wave(phase + LINK_STEP + 0x40000000u, 26), 148, 2);
    p->shot_multi_pos[SHOT_MISC] = 0;
    shot_create(0, SHOT_MISC, p->x + 1 + wave(phase - LINK_STEP, 26),
                p->y + wave(phase - LINK_STEP + 0x40000000u, 26), 148, 2);
    p->shot_multi_pos[SHOT_MISC] = 0;
    shot_create(0, SHOT_MISC, p->x + 1 + wave(phase, 26), p->y + wave(phase + 0x40000000u, 26), 147, 2);

    if (p->shot_repeat[REAR_WEAPON]) p->shot_repeat[REAR_WEAPON]--;
    else if (pad & PAD_B) {
        p->shot_multi_pos[REAR_WEAPON] = 0;
        /* Both original scripted GALAGA stages copy rear port 15 and have
         * no rear-port pickups: varz.c linkGunWeapons[14] is weapon 449. */
        shot = shot_create(0, REAR_WEAPON, p->x + 1 + wave(phase, 20),
                           p->y + wave(phase + 0x40000000u, 20), 449, 2);
        if (shot < PLAYER_SHOT_MAX) {
            PlayerShot *s = &player_shots[shot];
            int16_t speed = s->ym;
            s->xm = -wave(phase, speed);
            s->ym = -wave(phase + 0x40000000u, speed);
        }
    }
    if (p->x < 40) p->x = 40;
    if (p->x > 256) p->x = 256;
    if (p->y < 10) p->y = 10;
    if (p->y > 160) p->y = 160;
    p->delta_x_shot_move = p->x - p->last_x_shot_move;
    p->delta_y_shot_move = p->y - p->last_y_shot_move;
    bank = p->x_velocity / 2 + (p->x - mouse_x) / 6;
    p->banking = bank < -2 ? -2 : bank > 2 ? 2 : bank;
    if (p->invulnerable_ticks) p->invulnerable_ticks--;
    if (game_mode.charge_wait) game_mode.charge_wait--;
    else {
        if (game_mode.charge < 5) game_mode.charge++;
        game_mode.charge_wait = 28 - p->weapon_power[REAR_WEAPON] * 2;
        if (game.difficulty > DIFFICULTY_HARD) game_mode.charge_wait -= 5;
    }
    if (p->shot_repeat[5]) p->shot_repeat[5]--;
    else if ((pad & PAD_B) && game_mode.charge) {
        p->shot_multi_pos[5] = 0;
        shot_create(16, 5, p->x, p->y, 470 + game_mode.charge, 2);
        game_mode.charge = 0;
        game_mode.charge_wait = 30 - p->weapon_power[REAR_WEAPON] * 2;
    }
    if (p->superbombs) {
        if (p->shot_repeat[7]) p->shot_repeat[7]--;
        else if (pad & (PAD_A | PAD_Z)) {
            p->superbombs--; p->shot_multi_pos[7] = 0;
            shot_create(16, 7, p->x, p->y, 535, 2);
        }
    }
}

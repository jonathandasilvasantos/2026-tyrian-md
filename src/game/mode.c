#include "mode.h"
#include "game.h"
#include "player.h"
#include "password.h"
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
}

void mode_tick(void)
{
    Player *p = &players[0];
    if (!game_mode.galaga) return;
    p->shield = 0;
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
    game_mode.galaga = game_mode.extra = game_mode.no_backup = 0;
}

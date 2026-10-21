/* Checkpoints must survive episode/table changes and reject transcription
 * errors without replacing the current campaign. */
#include <stdio.h>
#include <string.h>
#include "game/game.h"
#include "game/episode.h"
#include "game/password.h"
#include "game/player.h"
#include "game/script.h"
#include "game/rng.h"
#include "gen_data/level.h"

static unsigned failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL password:%d %s\n", __LINE__, #c); failures++; } } while (0)

int main(void)
{
    unsigned ep, i;
    for (ep = 1; ep <= 4; ep++) {
        char code[49], after[49], damaged[49];
        Player *p = &players[0];
        game_new(ep, DIFFICULTY_HARD);
        game.initial_episode = 2; game.base_difficulty = DIFFICULTY_ZINGLON; game.repeated = 1;
        script_level_done(5, 0);
        CHECK(script.save_section == 5);
        p->ship = 13; p->generator = 6; p->shield_item = 10;
        p->weapon_id[0] = 42; p->weapon_id[1] = 41;
        p->weapon_power[0] = 11; p->weapon_power[1] = 8; p->weapon_mode = 2;
        p->option_id[0] = 30; p->option_id[1] = 29;
        p->cash = 0xfedcba98; rng_seed(0x89abcdef);
        game.cube_max = 9; script.shop_song = 41;
        password_encode(code);
        CHECK(strlen(code) == PASSWORD_LENGTH);
        game_new(ep == 4 ? 1 : ep + 1, DIFFICULTY_EASY);
        CHECK(password_load(code));
        CHECK(LEVEL_EPISODE == ep && game.initial_episode == 2);
        CHECK(game.base_difficulty == DIFFICULTY_ZINGLON && game.initial_difficulty == DIFFICULTY_HARD);
        CHECK(p->ship == 13 && p->generator == 6 && p->shield_item == 10);
        CHECK(p->weapon_id[0] == 42 && p->weapon_power[0] == 11 && p->weapon_power[1] == 8);
        CHECK(p->option_id[0] == 30 && p->option_id[1] == 29 && p->weapon_mode == 2);
        CHECK(p->cash == 0xfedcba98 && rng_state == 0x89abcdef);
        CHECK(script.section == 5 && script.save_section == 5 && script.shop_song == 41);
        CHECK(game.cube_max == 9 && game.repeated);
        password_encode(after);
        CHECK(strcmp(code, after) == 0);
        for (i = 0; i < PASSWORD_LENGTH; i++) {
            strcpy(damaged, code);
            damaged[i] = damaged[i] == '2' ? '3' : '2';
            CHECK(!password_load(damaged));
            password_encode(after);
            CHECK(strcmp(after, code) == 0 && LEVEL_EPISODE == ep);
        }
        CHECK(!password_load(""));
        strcpy(damaged, code); damaged[3] = '0';
        CHECK(!password_load(damaged));
        for (i = 0; i < PASSWORD_LENGTH; i++)
            damaged[i] = code[i] >= 'A' && code[i] <= 'Z' ? code[i] + 'a' - 'A' : code[i];
        CHECK(password_load(damaged));
    }
    if (failures) return 1;
    puts("   PASS  password checkpoints (four episodes, corruption rejection)");
    return 0;
}

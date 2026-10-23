/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
#include "password.h"
#include "episode.h"
#include "game.h"
#include "player.h"
#include "rng.h"
#include "script.h"
#include "gen_data/episodes.h"
#include "gen_data/items.h"
#include "gen_data/level.h"
#include "gen_data/script.h"

const char password_alphabet[33] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

static uint16_t checksum(const uint8_t *data)
{
    uint16_t crc = 0xffff;
    uint8_t i, b;
    for (i = 0; i < 28; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (b = 0; b < 8; b++) crc = (crc << 1) ^ (crc & 0x8000 ? 0x1021 : 0);
    }
    return crc;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint16_t)p[2] << 8 | p[3];
}

void password_encode(char out[PASSWORD_LENGTH + 1])
{
    const Player *p = &players[0];
    uint8_t data[30], i, n = 0, bits = 0;
    uint16_t crc, buffer = 0;
    /* Versioned, explicit byte order: never serialize C struct padding. */
    data[0] = 1; data[1] = LEVEL_EPISODE; data[2] = game.initial_episode;
    data[3] = game.initial_difficulty; data[4] = game.base_difficulty;
    data[5] = game.repeated; data[6] = script.save_section;
    data[7] = p->ship; data[8] = p->generator; data[9] = p->shield_item;
    data[10] = p->weapon_id[0]; data[11] = p->weapon_id[1];
    data[12] = p->weapon_power[0] | p->weapon_power[1] << 4;
    data[13] = p->weapon_mode; data[14] = p->option_id[0]; data[15] = p->option_id[1];
    put32(data + 16, p->cash); put32(data + 20, rng_state);
    /* config.c:JE_saveGame stores lastCubeMax, before ]? / ]! / ]+. */
    data[24] = script.save_cube_max >> 8; data[25] = script.save_cube_max;
    data[26] = script.shop_song; data[27] = p->special_id;
    crc = checksum(data); data[28] = crc >> 8; data[29] = crc;
    for (i = 0; i < 30; i++) {
        buffer = (buffer << 8) | data[i]; bits += 8;
        while (bits >= 5) { bits -= 5; out[n++] = password_alphabet[(buffer >> bits) & 31]; }
    }
    out[n] = 0;
}

uint8_t password_load(const char *text)
{
    uint8_t data[30], i, n = 0, bits = 0, old_episode = LEVEL_EPISODE, valid;
    uint16_t buffer = 0, crc;
    Player *p = &players[0];
    for (i = 0; i < PASSWORD_LENGTH; i++) {
        uint8_t value;
        char c = text[i];
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        for (value = 0; value < 32 && password_alphabet[value] != c; value++);
        if (value == 32) return 0; /* also rejects early string terminators */
        buffer = (buffer << 5) | value; bits += 5;
        if (bits >= 8) { bits -= 8; data[n++] = buffer >> bits; }
    }
    if (text[PASSWORD_LENGTH]) return 0;
    crc = checksum(data);
    if (data[0] != 1 || data[28] != (crc >> 8) || data[29] != (crc & 255)) return 0;
    if (data[1] < 1 || data[1] > EPISODE_COUNT || data[2] < 1 || data[2] > EPISODE_COUNT ||
        data[3] < 1 || data[3] > 9 || data[4] < data[3] || data[4] > DIFFICULTY_10 ||
        data[5] > 1 || !data[6] || data[7] > SHIP_NUM || data[8] > POWER_NUM ||
        data[9] > SHIELD_NUM || data[10] > PORT_NUM || data[11] > PORT_NUM ||
        !(data[12] & 15) || (data[12] & 15) > 11 || !(data[12] >> 4) || (data[12] >> 4) > 11 ||
        data[13] < 1 || data[13] > 2 || data[14] > OPTION_NUM || data[15] > OPTION_NUM ||
        !get32(data + 20) || data[26] < 1 || data[26] > 41 || data[27] > SPECIAL_NUM) return 0;
    episode_tables_select(data[1]);
    valid = data[6] <= SCRIPT_SECTIONS;
    episode_tables_select(old_episode);
    if (!valid) return 0;

    game_new(data[1], data[3]);
    game.initial_episode = data[2]; game.base_difficulty = data[4]; game.repeated = data[5];
    game.difficulty = data[4] - (data[1] == 4 && data[4] > DIFFICULTY_EASY);
    p->special_id = data[27];
    p->ship = data[7]; p->generator = data[8]; p->shield_item = data[9];
    p->weapon_id[0] = data[10]; p->weapon_id[1] = data[11];
    p->weapon_power[0] = data[12] & 15; p->weapon_power[1] = data[12] >> 4;
    p->weapon_mode = data[13]; p->option_id[0] = data[14]; p->option_id[1] = data[15];
    p->cash = get32(data + 16); rng_seed(get32(data + 20));
    game.cube_max = script.save_cube_max = (uint16_t)data[24] << 8 | data[25];
    script_goto(data[6]); script.save_section = data[6]; script.shop_song = data[26];
    return 1;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * See LICENSE and NOTICE.
 */
/* Host build of src/game: runs a pad replay headless and prints the state
 * hash, so the same replay can be compared with the ROM (agents.md §5).
 *
 * Usage: tyrian_host [frames] [replay.txt] [episode] [left-option] [right-option] [bombs] [special]
 *   replay lines: "<frame> <pad hex>"  (pad held from that frame on)
 */
#include <stdio.h>
#include <stdlib.h>
#include "game/game.h"
#include "game/episode.h"
#include "gen_data/level.h"
#include "gen_data/items.h"
#include "game/player.h"
#include "game/special.h"
#include "game/mode.h"
#include "game/enemy.h"

int main(int argc, char **argv)
{
    unsigned frames = argc > 1 ? (unsigned)atoi(argv[1]) : 600;
    unsigned next_frame = 0, next_pad = 0, pad = 0, f;
    FILE *fp = NULL;
    int have_next = 0;

    if (argc > 2 && !(fp = fopen(argv[2], "r"))) { perror(argv[2]); return 1; }

    game_new(argc > 3 ? (uint8_t)atoi(argv[3]) : LEVEL_DEFAULT_EPISODE, DIFFICULTY_NORMAL);
    if (argc > 4) {
        int left = atoi(argv[4]), right = argc > 5 ? atoi(argv[5]) : 0;
        if (left < 0 || left > OPTION_NUM || right < 0 || right > OPTION_NUM) {
            fprintf(stderr, "sidekick IDs must be 0..%d\n", OPTION_NUM);
            return 2;
        }
        players[0].option_id[0] = left;
        players[0].option_id[1] = right;
        game_start_level(LEVEL_FIRST);
    }
    if (getenv("B68K_MODE") && atoi(getenv("B68K_MODE"))) {
        int mode = atoi(getenv("B68K_MODE"));
        int level = getenv("B68K_LEVEL") ? atoi(getenv("B68K_LEVEL")) : 0;
        if (mode < 1 || mode > 2 || level < 1 || level > LEVEL_COUNT) return 2;
        mode_engage();
        if (mode == 2) mode_galaga();
    }
    if (getenv("B68K_LEVEL")) {
        int level = atoi(getenv("B68K_LEVEL"));
        if (level < 1 || level > LEVEL_COUNT) return 2;
        game_start_level(level - 1);
    }
    if (getenv("B68K_BUDDY") && atoi(getenv("B68K_BUDDY"))) {
        if (!game_mode.galaga) return 2;
        mode_collect_buddy(1);
    }
    if (getenv("B68K_BONUS") && atoi(getenv("B68K_BONUS"))) {
        int slot = enemy_new(0, 577, 0);
        if (slot) { enemies[slot - 1].ex = players[0].x; enemies[slot - 1].ey = players[0].y; }
    }
    if (argc > 6) {
        int bombs = atoi(argv[6]);
        if (bombs < 0 || bombs > 10) return 2;
        players[0].superbombs = bombs;
    }
    if (argc > 7 && !special_equip((uint8_t)atoi(argv[7]))) return 2;
    for (f = 0; f < frames; f++) {
        char line[128];
        while (fp) {
            if (!have_next) {
                if (!fgets(line, sizeof line, fp)) { fclose(fp); fp = NULL; break; }
                if (line[0] == '#' || sscanf(line, "%u %x", &next_frame, &next_pad) != 2) continue;
                have_next = 1;
            }
            if (next_frame > f) break;
            pad = next_pad;
            have_next = 0;
        }
        game_step((uint16_t)pad);
    }
    if (fp) fclose(fp);
    printf("frames %u hash %08x\n", frames, game_hash());
    return 0;
}

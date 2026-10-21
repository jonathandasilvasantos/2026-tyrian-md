/* Host build of src/game: runs a pad replay headless and prints the state
 * hash, so the same replay can be compared with the ROM (agents.md §5).
 *
 * Usage: tyrian_host [frames] [replay.txt]
 *   replay lines: "<frame> <pad hex>"  (pad held from that frame on)
 */
#include <stdio.h>
#include <stdlib.h>
#include "game/game.h"
#include "game/episode.h"
#include "gen_data/level.h"

int main(int argc, char **argv)
{
    unsigned frames = argc > 1 ? (unsigned)atoi(argv[1]) : 600;
    unsigned next_frame = 0, next_pad = 0, pad = 0, f;
    FILE *fp = NULL;
    int have_next = 0;

    if (argc > 2 && !(fp = fopen(argv[2], "r"))) { perror(argv[2]); return 1; }

    game_new(argc > 3 ? (uint8_t)atoi(argv[3]) : LEVEL_DEFAULT_EPISODE, DIFFICULTY_NORMAL);
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

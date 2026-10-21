/* script.c — episode script interpreter: runs a section's commands until
 * one needs the player (a level or a shop).  Text and picture screens are
 * not compiled in yet (agents.md §8). */
#include "script.h"
#include "game.h"
#include "player.h"
#include "difficulty.h"
#include "gen_data/script.h"

#define DEFAULT_SONG_BUY 2   /* musmast.h: play_song(DEFAULT_SONG_BUY) */

ScriptState script;

static void go_section(uint8_t sec)
{
    if (sec == 0 || sec > SCRIPT_SECTIONS) sec = script.section + 1;
    script.section = sec;
    script.pc = script_sections[sec];
}

void script_start(void)
{
    script.choices = 0;
    script.died = script.timer_expired = 0;
    script.save_section = 1;
    script.shop_song = DEFAULT_SONG_BUY + 1;
    go_section(1);
}

FlowStep script_run(void)
{
    FlowStep st;
    uint8_t skip = 0;

    for (;;) {
        const uint8_t *op = &script_ops[script.pc];
        uint8_t len = 1;
        if (skip && op[0] != OP_END) {   /* ]h: the next command line is skipped */
            skip = 0;
            switch (op[0]) {
            case OP_JUMP: case OP_JUMP_IF_EASY: case OP_JUMP_IF_DIED: case OP_JUMP_IF_TIMER:
            case OP_JUMP_IF_SHIP13: case OP_SHOP: case OP_SHOP_SONG: len = 2; break;
            case OP_LEVEL: len = 6; break;
            case OP_CHOOSE: len = 2 + 2 * op[1]; break;
            }
            script.pc += len;
            continue;
        }
        switch (op[0]) {
        case OP_END:
        case OP_END_EPISODE:
            st.kind = FLOW_END;
            return st;
        case OP_JUMP:
            go_section(op[1]);
            continue;
        case OP_LEVEL:
            st.kind = FLOW_LEVEL;
            st.next_section = op[1] ? op[1] : script.section + 1;
            st.level = op[2];
            st.song = op[3];
            st.flags = op[4];
            st.name = op[5];
            /* pc stays on the command: a death replays it */
            return st;
        case OP_SHOP:
            st.kind = FLOW_SHOP;
            st.shop = op[1];
            script.pc += 2;
            return st;
        case OP_CHOOSE: {
            uint8_t i, n = op[1] > 4 ? 4 : op[1];
            script.choices = n;
            for (i = 0; i < n; i++) {
                script.choice_planet[i] = op[2 + 2 * i];
                script.choice_section[i] = op[3 + 2 * i];
            }
            len = 2 + 2 * op[1];
            break;
        }
        case OP_SKIP_IF_HARD:
            skip = game.initial_difficulty > DIFFICULTY_NORMAL;
            break;
        case OP_JUMP_IF_EASY:
            if (game.initial_difficulty < DIFFICULTY_NORMAL + 1) {
                go_section(op[1]);
                continue;
            }
            len = 2;
            break;
        case OP_SHOP_SONG:
            script.shop_song = op[1];
            len = 2;
            break;
        case OP_SAVE:
            script.save_section = script.section;
            break;
        case OP_JUMP_IF_DIED:
        case OP_JUMP_IF_TIMER:
        case OP_JUMP_IF_SHIP13:
            if ((op[0] == OP_JUMP_IF_DIED && script.died) ||
                (op[0] == OP_JUMP_IF_TIMER && script.timer_expired) ||
                (op[0] == OP_JUMP_IF_SHIP13 && players[0].ship == 13)) {
                go_section(op[1]);
                continue;
            }
            len = 2;
            break;
        default:
            break;
        }
        script.pc += len;
    }
}

void script_goto(uint8_t section)
{
    if (section) go_section(section);
}

void script_level_done(uint8_t next_section, uint8_t died)
{
    script.died = died;
    if (!died) {
        game_level_complete();
        go_section(next_section);
        /* JE_loadMap initializes saveLevel on entry, before internal jumps. */
        script.save_section = script.section;
    }
}

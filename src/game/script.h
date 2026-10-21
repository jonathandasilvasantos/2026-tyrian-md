/* script.h — episode script interpreter (OpenTyrian tyrian2.c:JE_loadMap's
 * section loop), over the bytecode from tools/tyrdata/script.py. */
#ifndef GAME_SCRIPT_H
#define GAME_SCRIPT_H

#include "types.h"

enum { FLOW_LEVEL, FLOW_SHOP, FLOW_END, FLOW_TEXT, FLOW_PICTURE, FLOW_MUSIC, FLOW_FADE, FLOW_SCENE_RESET };

typedef struct {
    uint8_t kind;
    /* FLOW_LEVEL */
    uint8_t level, next_section, song, flags, name;
    /* FLOW_SHOP */
    uint8_t shop;
} FlowStep;

typedef struct {
    uint16_t pc;
    uint8_t section, save_section;
    uint8_t choices, choice_planet[4], choice_section[4];
    uint8_t died, timer_expired;
    uint8_t shop_song;        /* songBuy, 1-based (]i) */
} ScriptState;

extern ScriptState script;

void script_start(void);
FlowStep script_run(void);
/* after a shop: go to the chosen destination section (0 = stay) */
void script_goto(uint8_t section);
/* after a level: next section on success, the same level again on death */
void script_level_done(uint8_t next_section, uint8_t died);

#endif

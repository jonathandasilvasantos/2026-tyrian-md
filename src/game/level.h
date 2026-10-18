/* level.h — level events and background scrolling (OpenTyrian tyrian2.c
 * JE_eventSystem, JE_createNewEventEnemy, JE_eventJump; backgrnd.c). */
#ifndef GAME_LEVEL_H
#define GAME_LEVEL_H

#include "types.h"

/* layer widths in map tiles (mapYPos steps) */
#define LAYER1_W 14
#define LAYER2_W 14
#define LAYER3_W 15

void level_init(void);
void level_run_events(void);            /* run due events */
void level_scroll_1(void);            /* background 1 movement (and curLoc) */
void level_scroll_2(void);
void level_scroll_3(void);
void level_wrap(void);                /* BKwrap checks at loop start */
void level_event_jump(uint16_t jump);
/* JE_searchFor: live enemy with that link number; index in *out (may be NULL) */
uint8_t level_search_for(uint8_t link, uint8_t *out);
/* top screen row (mapY) of a layer, for rendering */
int16_t level_layer_row(int layer);

#endif

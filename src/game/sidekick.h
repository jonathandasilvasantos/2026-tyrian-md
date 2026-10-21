/* Companions: mainint.c JE_playerMovement and varz.c JE_drawOptions. */
#ifndef GAME_SIDEKICK_H
#define GAME_SIDEKICK_H
#include "types.h"
void sidekicks_init(void);
void sidekicks_before_move(void);
void sidekicks_record_move(int16_t old_x, int16_t old_y);
void sidekicks_tick(uint16_t pad);
void sidekicks_animate(void);
#endif

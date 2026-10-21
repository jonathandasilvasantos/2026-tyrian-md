/* Scripted ENGAGE/GALAGA modes (tyrian2.c:JE_loadMap, mainint.c). */
#ifndef GAME_MODE_H
#define GAME_MODE_H
#include "types.h"
typedef struct {
    uint32_t next_life, link_phase;
    uint8_t arcade, super_tyrian, galaga, extra, no_backup, shot_frequency;
    uint8_t buddy, charge, charge_wait;
} GameMode;
extern GameMode game_mode;
void mode_reset(void);
void mode_checkpoint(void);
uint8_t mode_restore(void);
void mode_engage(void);
void mode_galaga(void);
void mode_level_start(void);
void mode_tick(void);
uint8_t mode_respawn(void);
void mode_leave_level(void);
void mode_collect_buddy(uint8_t player_num);
void mode_buddy_tick(uint16_t pad);
#endif

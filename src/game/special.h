/* Equipped story specials: OpenTyrian varz.c JE_doSpecialShot. */
#ifndef GAME_SPECIAL_H
#define GAME_SPECIAL_H
#include "types.h"
typedef struct {
    uint16_t flare, astral, weapon;
    int8_t filter, frequency;
    uint8_t wait, next_wait, flare_start, linked, held, zinglon, pulse, spray;
    uint8_t combo[21], executed;
} SpecialState;
extern SpecialState special_state;
void special_init(void);
void special_combo(uint8_t symbol);
void special_tick(uint16_t pad);
uint8_t special_equip(uint8_t id);
#endif

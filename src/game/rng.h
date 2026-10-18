/* rng.h — game RNG (replaces OpenTyrian's mt_rand; agents.md §8). */
#ifndef GAME_RNG_H
#define GAME_RNG_H

#include "types.h"

extern uint32_t rng_state;

void rng_seed(uint32_t seed);
uint16_t rng16(void);
/* mt_rand() % n for n in 1..65535; 16-bit math (one divu on the 68000). */
uint16_t rng_mod(uint16_t n);

#endif

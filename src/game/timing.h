/* 35 simulation ticks/sec on 60 Hz NTSC and 50 Hz PAL displays.
 * Render interpolation retains the engine's 0..11 fractional convention. */
#ifndef GAME_TIMING_H
#define GAME_TIMING_H
#include "game.h"

typedef struct { uint8_t accumulator, denominator; } TickClock;

static inline void tick_clock_init(TickClock *clock, uint8_t pal)
{
    clock->accumulator = 0;
    clock->denominator = pal ? 10 : TICK_DEN;
}

static inline uint8_t tick_clock_step(TickClock *clock)
{
    clock->accumulator += TICK_NUM;
    if (clock->accumulator < clock->denominator) return 0;
    clock->accumulator -= clock->denominator;
    return 1;
}

static inline uint8_t tick_clock_fraction(const TickClock *clock)
{
    /* round(accumulator * 12 / 10), precomputed: no per-frame divide. */
    static const uint8_t pal_fraction[10] = {0, 1, 2, 4, 5, 6, 7, 8, 10, 11};
    return clock->denominator == 10 ? pal_fraction[clock->accumulator] : clock->accumulator;
}
#endif

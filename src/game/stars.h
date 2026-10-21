/* OpenTyrian backgrnd.c starfield. Positions intentionally wrap at 16 bits. */
#ifndef GAME_STARS_H
#define GAME_STARS_H
#include "types.h"
#define STAR_COUNT 100
typedef struct { uint16_t position; uint8_t speed, color; } Star;
typedef struct {
    Star star[STAR_COUNT];
    int16_t speed;
    uint8_t active, initialized, draw_astral;
} Starfield;
extern Starfield starfield;
void stars_reset(void);
void stars_begin(void);
void stars_update(void);
/* Exact quotient/remainder by 320 without DIVU in the renderer. */
static inline void star_coordinates(uint16_t position, uint16_t *x, uint16_t *y)
{
    uint16_t row = ((uint32_t)position * 205) >> 16;
    uint16_t col = position - row * 320;
    if (col > 319) { col += 320; row--; }
    *x = col; *y = row;
}
#endif

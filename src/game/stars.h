/* OpenTyrian backgrnd.c starfield. Positions intentionally wrap at 16 bits. */
#ifndef GAME_STARS_H
#define GAME_STARS_H
#include "types.h"
#include "gen_data/motion.h"
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
/* One fixed table lookup replaces two MULU instructions in sprite builds.
 * High-byte contribution to quotient/remainder; low byte needs one carry. */
static inline void star_coordinates(uint16_t position, uint16_t *x, uint16_t *y)
{
    uint32_t base = star_position_base[position >> 8];
    uint16_t col = (uint16_t)base + (position & 255);
    uint16_t row = base >> 16;
    if (col >= 320) { col -= 320; row++; }
    *x = col; *y = row;
}

/* Screen transform is prepared once per sprite build. Both masks use the
 * renderer's 48-column resident-cell stride; NULL means an Astral-hidden plane.
 * Narrow arithmetic is deliberate: the 68000 needs only the low eight Y bits. */
typedef struct {
    const uint8_t *cells;
    int16_t x, y;
    uint16_t columns;
} StarMask;
static inline uint8_t star_mask_clear(const StarMask *mask, int16_t x, int16_t y)
{
    int16_t col = (int16_t)(x + mask->x) >> 3;
    uint16_t row;
    if (!mask->cells || (uint16_t)col >= mask->columns) return 1;
    row = ((uint16_t)(y + mask->y) >> 3) & 31;
    return mask->cells[(uint16_t)(row * 48 + col)] != 0;
}
#endif

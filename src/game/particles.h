/* JE_doSP / JE_drawSP superpixels. Cosmetic cap: 64 (PC 101). */
#ifndef GAME_PARTICLES_H
#define GAME_PARTICLES_H
#include "types.h"
#define PARTICLE_MAX 64
typedef struct {
    int16_t x, y;
    int8_t dx, dy;
    uint8_t color, life; /* PC z+1 after a tick: retain the final visible frame */
} Particle;
typedef struct {
    Particle item[PARTICLE_MAX];
    uint8_t last, live;
} ParticleState;
extern ParticleState particles;
/* Encoded weapon frames 1001..60000: exact /1000 and remainder without DIVU.
 * /1024 underestimates the quotient by at most two in this bounded range. */
static inline uint16_t particle_frame(uint16_t frame, uint8_t *color)
{
    uint16_t q = frame >> 10;
    frame -= q * 1000;
    if (frame >= 1000) { frame -= 1000; q++; }
    if (frame >= 1000) { frame -= 1000; q++; }
    *color = (uint8_t)(q << 4);
    return frame;
}
void particles_reset(void);
/* Original weapon/impact data bounds width to 0..64. */
void particles_spawn(int16_t x, int16_t y, uint16_t count, uint8_t width, uint8_t color);
void particles_tick(void);
#endif

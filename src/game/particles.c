/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
#include "particles.h"
#include "rng.h"
#include "gen_data/motion.h"

ParticleState particles;
_Static_assert(sizeof(Particle) == 8, "particle RAM budget");

void particles_reset(void)
{
    uint16_t i;
    for (i = 0; i < PARTICLE_MAX; i++) particles.item[i].life = 0;
    particles.last = particles.live = 0;
}

/* sin/cos and independent radial samples, as JE_doSP. Quantize the angle
 * to 1024 steps and radius to Q8; two signed 16x16 multiplies replace floats.
 * roundf's negative half case rounds away from zero. Width is at most 64. */
static int8_t velocity(int16_t direction, uint16_t sample, uint8_t width)
{
    int16_t radius = (uint16_t)((uint32_t)sample * width >> 8);
    int32_t value = (int32_t)direction * radius;
    return value < 0 ? -(int8_t)((-value + (1L << 22)) >> 23) :
                       (int8_t)((value + (1L << 22)) >> 23);
}

void particles_spawn(int16_t x, int16_t y, uint16_t count, uint8_t width, uint8_t color)
{
    while (count--) {
        uint16_t angle = rng16() >> 6;
        int8_t dy = velocity(particle_sine[(angle + 256) & 1023], rng16(), width);
        int8_t dx = velocity(particle_sine[angle], rng16(), width);
        Particle *p;
        if (++particles.last == PARTICLE_MAX) particles.last = 0;
        p = &particles.item[particles.last];
        if (!p->life) particles.live++;
        p->x = x + dx; p->y = y + dy;
        p->dx = dx; p->dy = dy + 1;
        p->color = color;
        p->life = 16;
    }
}

void particles_tick(void)
{
    uint16_t i;
    if (!particles.live) return;
    for (i = 0; i < PARTICLE_MAX; i++) {
        Particle *p = &particles.item[i];
        if (!p->life) continue;
        if (--p->life) {
            p->x += p->dx;
            p->y += p->dy;
        } else particles.live--;
    }
}

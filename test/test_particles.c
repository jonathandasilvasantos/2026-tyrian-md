#include <stdio.h>
#include "game/particles.h"
#include "game/rng.h"
static unsigned failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL particles:%d %s\n", __LINE__, #c); failures++; } } while (0)
int main(void)
{
    uint32_t i, expected;
    uint8_t color;
    for (i = 1001; i <= 60000; i++) {
        CHECK(particle_frame(i, &color) == i % 1000);
        CHECK(color == (uint8_t)((i / 1000) << 4));
    }
    particles_reset();
    CHECK(!particles.live && !particles.last);
    rng_seed(123);
    for (i = 0; i < 3 * 101; i++) rng16();
    expected = rng_state;
    rng_seed(123);
    particles_spawn(100, 80, 101, 3, 0xD0);
    CHECK(rng_state == expected); /* dropped/overwritten particles still consume RNG */
    CHECK(particles.live == PARTICLE_MAX && particles.last == 101 % PARTICLE_MAX);
    for (i = 0; i < PARTICLE_MAX; i++) {
        const Particle *p = &particles.item[i];
        CHECK(p->life == 16 && p->color == 0xD0);
        CHECK(p->dx >= -3 && p->dx <= 3 && p->dy >= -2 && p->dy <= 4);
        CHECK(p->x == 100 + p->dx && p->y == 79 + p->dy);
    }
    {
        Particle before = particles.item[0];
        particles_tick();
        CHECK(particles.item[0].x == before.x + before.dx);
        CHECK(particles.item[0].y == before.y + before.dy);
        CHECK(particles.item[0].life == 15);
    }
    for (i = 1; i < 15; i++) particles_tick();
    CHECK(particles.item[0].life == 1); /* last, dimmest visible PC frame */
    CHECK(particles.live == PARTICLE_MAX);
    particles_tick();
    CHECK(!particles.live && !particles.item[0].life);
    CHECK(rng_state == expected); /* drawing/updating consumes no RNG */
    particles_reset();
    particles_spawn(-1, -1, 1, 64, 0x20);
    for (i = 0; i < PARTICLE_MAX; i++) {
        const Particle *p = &particles.item[i];
        if (!p->life) continue;
        CHECK(p->dx >= -64 && p->dx <= 64 && p->dy >= -63 && p->dy <= 65);
    }
    particles_reset();
    particles_tick();
    CHECK(!particles.live && !particles.last);
    if (failures) return 1;
    puts("   PASS  superpixels (RNG, ring cap, life, motion, frame decoding)");
    return 0;
}

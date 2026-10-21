#include <stdio.h>
#include "game/stars.h"
#include "game/special.h"
#include "game/rng.h"
/* Only the Astral-duration input is needed by this isolated simulation test. */
SpecialState special_state;
static unsigned failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL stars:%d %s\n", __LINE__, #c); failures++; } } while (0)
int main(void)
{
    uint32_t i, expected_rng;
    uint16_t x, y;
    for (i = 0; i < 65536; i++) {
        star_coordinates((uint16_t)i, &x, &y);
        CHECK(x == i % 320 && y == i / 320);
    }
    rng_seed(123);
    for (i = 0; i < 400; i++) rng16();
    expected_rng = rng_state;
    rng_seed(123); stars_reset();
    CHECK(!starfield.initialized);
    stars_begin();
    CHECK(starfield.initialized && rng_state == expected_rng);
    stars_begin();
    CHECK(rng_state == expected_rng); /* no double initialization */
    for (i = 0; i < STAR_COUNT; i++) {
        CHECK(starfield.star[i].position < 64000);
        CHECK(starfield.star[i].speed >= 2 && starfield.star[i].speed <= 4);
        CHECK(starfield.star[i].color >= 0x90 && starfield.star[i].color < 0xa0);
    }
    starfield.star[99].position = 65530; starfield.star[99].speed = 2;
    stars_update();
    CHECK(starfield.star[99].position == 954 && rng_state == expected_rng);
    starfield.active = 0;
    stars_update();
    CHECK(starfield.star[99].position == 954 && !starfield.draw_astral);
    special_state.astral = 1;
    stars_update();
    CHECK(starfield.star[99].position == 1914 && starfield.draw_astral);
    special_state.astral = 0;
    stars_update();
    CHECK(starfield.star[99].position == 1914 && !starfield.draw_astral);
    if (failures) return 1;
    puts("   PASS  stars (16-bit wrap, initialization RNG, Astral override)");
    return 0;
}

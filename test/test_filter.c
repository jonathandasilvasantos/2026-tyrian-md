#include <stdio.h>
#include "game/filter.h"
static unsigned failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL filter:%d %s\n", __LINE__, #c); failures++; } } while (0)
int main(void)
{
    unsigned i;
    CHECK(filter_index(0x2e, 7, -99) == 0x7e);
    CHECK(filter_index(0x2f, -99, -14) == 0x21);
    CHECK(filter_index(0x2f, -99, 1) == 0x2f);
    CHECK(filter_index(0x2f, -99, 16) == 0x20); /* PC overflow above 0x1e */
    CHECK(filter_index(0x20, -99, 30) == 0x2f);
    CHECK(filter_index(0x20, -99, 31) == 0x20);
    CHECK(filter_index(0x28, -99, -99) == 0x28);
    filter_init();
    for (i = 0; i < 13; i++) filter_tick();
    CHECK(filter_state.draw_brightness == -2 && filter_state.brightness == -1);
    filter_tick();
    CHECK(filter_state.draw_brightness == -1 && filter_state.brightness == -99 && !filter_state.fade);
    filter_tick();
    CHECK(filter_state.draw_brightness == -99);
    filter_state = (FilterState){7, -14, 3, -1, 0, 0, 1, 1, 1};
    filter_tick();
    CHECK(filter_state.draw_hue == 7 && filter_state.draw_brightness == -14);
    CHECK(filter_state.hue == 3 && filter_state.delta == 1 && !filter_state.fade_start);
    filter_tick();
    CHECK(filter_state.draw_hue == 3 && filter_state.draw_brightness == -15);
    for (i = 0; i < 14; i++) filter_tick();
    CHECK(!filter_state.fade && filter_state.brightness == -99);
    filter_state.active = 0;
    filter_tick();
    CHECK(filter_state.draw_hue == -99 && filter_state.draw_brightness == -99);
    if (failures) return 1;
    puts("   PASS  palette filter boundaries and fade timing");
    return 0;
}

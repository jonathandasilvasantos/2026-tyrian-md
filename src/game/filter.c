#include "filter.h"
FilterState filter_state;

void filter_init(void)
{
    filter_state = (FilterState){-99, -14, 0, 1, -99, -14, 1, 1, 0};
}

void filter_tick(void)
{
    FilterState *f = &filter_state;
    f->draw_hue = f->active ? f->hue : -99;
    f->draw_brightness = f->active ? f->brightness : -99;
    /* JE_filterScreen receives col/brightness by value, then advances the
     * globals. Its new fade values become visible on the following tick. */
    if (f->active && f->fade) {
        f->brightness += f->delta;
        if ((f->fade_start && f->brightness < -14) || f->brightness > 14) {
            f->delta = -f->delta;
            f->fade_start = 0;
            f->hue = f->next_hue;
        }
        if (!f->fade_start && f->brightness == 0) {
            f->fade = 0;
            f->brightness = -99;
        }
    }
}

uint8_t filter_index(uint8_t color, int8_t hue, int8_t brightness)
{
    int16_t value = color & 15;
    if (hue != -99) color = ((uint8_t)hue << 4) | value;
    if (brightness != -99) {
        value += brightness;
        if (value < 0 || value > 30) value = 0;
        else if (value > 15) value = 15;
        color = (color & 240) | value;
    }
    return color;
}

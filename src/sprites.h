/* sprites.h — hardware sprite list: player ship and shots. */
#ifndef SPRITES_H
#define SPRITES_H

#include <genesis.h>

/* VRAM tiles used from first_tile: ship 12 + piece cache + big cache */
#define SPRITES_VRAM_TILES (12 + 88 * 4 + 20 * 12)

void sprites_init(u16 first_tile);
/* frac: progress towards the next game tick, 0..TICK_DEN-1; ticked: a
 * game tick ran this frame (only advance the previous list) */
void sprites_update(u16 frac, bool ticked);

#endif

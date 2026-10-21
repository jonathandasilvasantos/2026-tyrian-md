/* sprites.h — hardware sprite list: player ship and shots. */
#ifndef SPRITES_H
#define SPRITES_H

#include <genesis.h>

#include "gen_data/level_bg.h"

/* The background pool differs by episode. Keep the 20 big-enemy slots,
 * then fit the piece cache below the HUD/font at 0xC000. A cache miss can
 * skip a visual piece for this rebuild; simulation and collision stay
 * unchanged. The player ship and enemy bullets are submitted before enemy artwork.
 * See agents.md §8. */
#define SPRITES_BIG_SLOTS 20
#define SPRITES_PIECE_ROOM ((0xC000 / 32 - 96 - TILE_USER_INDEX - BG_POOL_NEED - 28 - 12 - SPRITES_BIG_SLOTS * 12) / 4)
#define SPRITES_PIECE_SLOTS ((SPRITES_PIECE_ROOM < 88) ? SPRITES_PIECE_ROOM : 88)
#define SPRITES_VRAM_TILES (12 + SPRITES_PIECE_SLOTS * 4 + SPRITES_BIG_SLOTS * 12)
_Static_assert(SPRITES_PIECE_SLOTS >= 16, "insufficient VRAM for sprite pieces");

void sprites_init(u16 first_tile);
/* frac: progress towards the next game tick, 0..TICK_DEN-1; ticked: a
 * game tick ran this frame (only advance the previous list) */
void sprites_update(u16 frac, bool ticked);

#endif

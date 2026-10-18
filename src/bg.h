/* bg.h — streaming background layers (agents.md §4 Backgrounds). */
#ifndef BG_H
#define BG_H

#include <genesis.h>

#include "gen_data/level_bg.h"

/* first VRAM tile after the background slot pool */
#define BG_VRAM_END (TILE_USER_INDEX + BG_POOL_NEED)

void bg_init(void);
/* once per 60 Hz frame; frac = progress towards the next tick, 0..11 */
void bg_update(u16 frac, bool light);

#endif

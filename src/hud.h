/* hud.h — in-game HUD on the window plane (right panel + bottom bar). */
#ifndef HUD_H
#define HUD_H

#include <genesis.h>

/* tiles: hud font at TILE_FONT_INDEX (96), bars + solid at first_tile */
void hud_init(u16 first_tile, const char *level_name);
void hud_update(void);       /* redraws what changed; call once per frame */
void hud_message(const char *text);   /* bottom bar text */

#endif

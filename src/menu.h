/* menu.h — text screens with Tyrian's SMALL_FONT (2 cells per char). */
#ifndef MENU_H
#define MENU_H

#include <genesis.h>

void menu_begin(void);                 /* black screen, font loaded, planes cleared */
void menu_text(u16 col, u16 row, const char *s, u16 width);   /* row in 16-px lines */
u16 menu_pad_edge(void);               /* buttons newly pressed this frame */
void menu_frame(void);                 /* end of frame */
/* upgrade shop + destination choice; returns the chosen section (0 = none) */
u8 shop_screen(u8 shop);
void message_screen(const char *line1, const char *line2);
u8 episode_screen(void);
u8 difficulty_screen(void);
void pause_screen(void);
const char *episode_name(u8 episode);
void title_screen(void);           /* Tyrian MD + credits, waits for START */

#endif

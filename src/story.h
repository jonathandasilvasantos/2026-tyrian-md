/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * See LICENSE and NOTICE.
 */
/* Story presentation owns no simulation state; all images/text are ROM tiles. */
#ifndef STORY_H
#define STORY_H
#include <genesis.h>
u16 story_background(u16 base);
void story_picture(u8 number);
void story_text(u8 page);
void story_fade(u8 type);
void story_reset(void);
void story_resume(void);
#endif

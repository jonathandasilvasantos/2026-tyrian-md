/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * See LICENSE and NOTICE.
 */
/* sound.h — sound effects through SGDK's XGM2 driver (3 PCM channels). */
#ifndef SOUND_H
#define SOUND_H

#include <genesis.h>

void sound_init(void);
void sound_flush(void);          /* play and clear game.sound_queue */
void sound_pause(bool paused);
void sound_music(u8 song);       /* 1-based song, 0 = stop; no-op if already playing */

#endif

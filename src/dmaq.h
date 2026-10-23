/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * See LICENSE and NOTICE.
 */
/* dmaq.h — lean VRAM DMA queue for the per-frame tile uploads. */
#ifndef DMAQ_H
#define DMAQ_H

#include <genesis.h>

void dmaq_init(void);
/* Queue a physical-ROM/RAM -> VRAM DMA of len words (even src).
 * Flush maps far graphics and splits VDP source boundaries. */
void dmaq_vram(const void *src, u16 dst, u16 len);
/* run the queue now (vblank callback; also init code with a full queue) */
void dmaq_flush(void);

#endif

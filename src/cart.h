/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * See LICENSE and NOTICE.
 */
/* SSF2 cartridge: lower 3.5 MB fixed, upper 512 KB used for graphics DMA. */
#ifndef CART_H
#define CART_H
#include <genesis.h>
#define CART_ROM_BYTES 0x800000UL
#define CART_FIXED_BYTES 0x380000UL
void cart_init(void);
/* Called with the Z80 bus held and no DMA pending. Never use for audio. */
u32 cart_map_dma(u32 physical);
#endif

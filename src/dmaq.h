/* dmaq.h — lean VRAM DMA queue for the per-frame tile uploads. */
#ifndef DMAQ_H
#define DMAQ_H

#include <genesis.h>

void dmaq_init(void);
/* Queue a 68000 -> VRAM DMA of len words (even src, no 128 KB crossing:
 * every caller copies <= 192 bytes from 32-byte aligned ROM tiles). */
void dmaq_vram(const void *src, u16 dst, u16 len);
/* run the queue now (vblank callback; also init code with a full queue) */
void dmaq_flush(void);

#endif

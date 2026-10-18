/* dmaq.c — lean VRAM DMA queue.
 *
 * SGDK's DMA_queueDma costs ~700 cycles per entry on the 68000 (bank split
 * check, register images built at queue time); a burst of explosion frames
 * or background tiles queues 20-40 of them.  Here queueing is three stores
 * and the registers are written at flush time, from SGDK's vblank callback
 * (SYS_doVBlankProcess, after its own DMA queue, still inside vblank).
 * Not handled: sources crossing a 128 KB boundary (callers move <= 192
 * bytes from 32-byte aligned tiles, which never cross one).
 * The Z80 (XGM2 sound driver) is halted during the transfers, as SGDK's
 * own flush does (HALT_Z80_ON_DMA): a Z80 read of the 68000 bus during a
 * 68000->VDP DMA can corrupt either.
 */
#include "dmaq.h"

#define DMAQ_MAX 64

static u32 q_src[DMAQ_MAX];
static u16 q_dst[DMAQ_MAX], q_len[DMAQ_MAX];
static u16 q_n;

void dmaq_flush(void)
{
    vu16 *ctrl = (vu16 *)VDP_CTRL_PORT;
    vu32 *ctrl32 = (vu32 *)VDP_CTRL_PORT;
    u16 i;

    bool bus_taken;

    if (!q_n) return;
    bus_taken = Z80_isBusTaken();
    if (!bus_taken) Z80_requestBus(FALSE);
    *ctrl = 0x8F02;                               /* auto-increment 2 */
    for (i = 0; i < q_n; i++) {
        u32 src = q_src[i] >> 1;
        u16 len = q_len[i], dst = q_dst[i];
        *ctrl = 0x9300 | (len & 0xFF);
        *ctrl = 0x9400 | (len >> 8);
        *ctrl = 0x9500 | (src & 0xFF);
        *ctrl = 0x9600 | ((src >> 8) & 0xFF);
        *ctrl = 0x9700 | ((src >> 16) & 0x7F);
        /* VRAM write with DMA: CD = 100001 */
        *ctrl32 = 0x40000080 | ((u32)(dst & 0x3FFF) << 16) | (dst >> 14);
    }
    if (!bus_taken) Z80_releaseBus();
    q_n = 0;
}

void dmaq_init(void)
{
    q_n = 0;
    SYS_setVBlankCallback(dmaq_flush);
}

void dmaq_vram(const void *src, u16 dst, u16 len)
{
    u16 i = q_n;
    if (i >= DMAQ_MAX) return;      /* dropped: a stale tile for a frame */
    q_src[i] = (u32)src;
    q_dst[i] = dst;
    q_len[i] = len;
    q_n = i + 1;
}

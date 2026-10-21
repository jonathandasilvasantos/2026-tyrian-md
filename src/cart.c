#include "cart.h"

void cart_init(void)
{
    /* A soft reset can leave the physical mapper on the last graphics bank. */
    SYS_resetBanks();
}

u32 cart_map_dma(u32 physical)
{
    u16 bank;
    if (physical < CART_FIXED_BYTES || physical >= 0xE00000) return physical;
    if (physical >= CART_ROM_BYTES) SYS_die("cart: graphics past 8 MB");
    bank = physical >> 19;
    /* All gameplay tables/code/audio remain below this window.
     * SGDK's explicit mapper API works with automatic FAR access off. */
    if (SYS_getBank(7) != bank) SYS_setBank(7, bank);
    return CART_FIXED_BYTES + (physical & 0x7FFFF);
}

#include "genesis.h"

__attribute__((externally_visible))
const ROMHeader rom_header = {
#if (ENABLE_BANK_SWITCH != 0)
    "SEGA SSF        ",
#elif (MODULE_MEGAWIFI != 0)
    "SEGA MEGAWIFI   ",
#else
    "SEGA MEGA DRIVE ",
#endif
    "(C)TYRN 2026    ",
    "TYRIAN MD                                       ",
    "TYRIAN MD                                       ",
    "GM 00000000-00",
    0x000,
    "JD              ",
    0x00000000,
    /* make rom pads each selected episode to the 4 MB linear cart.
     * A future combined 8 MB cartridge also needs explicit mapping. */
    0x003FFFFF,
    0xE0FF0000,
    0xE0FFFFFF,
    "  ",
    0x2020,
    0x20202020,
    0x20202020,
    "            ",
    "                                        ",
    "JUE             "
};

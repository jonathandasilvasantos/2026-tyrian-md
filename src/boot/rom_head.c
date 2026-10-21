#include "genesis.h"

__attribute__((externally_visible))
const ROMHeader rom_header = {
    "SEGA SSF        ",  /* official Sega mapper, fixed audio below 3.5 MB */
    "(C)TYRN 2026    ",
    "TYRIAN MD                                       ",
    "TYRIAN MD                                       ",
    "GM 00000000-00",
    0x000,
    "JD              ",
    0x00000000,
    0x007FFFFF,
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

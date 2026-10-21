#include "cubes.h"
#include "script.h"
#include "game.h"
#include "gen_data/level.h"

const CubeDef *cube_definition(uint8_t episode, uint8_t id)
{
    if (episode < 1 || episode > 4 || !id || id > cube_counts[episode - 1]) return 0;
    return &cube_defs[cube_offsets[episode - 1] + id - 1];
}

uint8_t cube_available_count(void)
{
    return game.cube_max > 4 ? 4 : (uint8_t)game.cube_max;
}

const CubeDef *cube_available(uint8_t slot)
{
    if (slot >= cube_available_count()) return 0;
    return cube_definition(LEVEL_EPISODE, script.cube_list[slot]);
}

uint16_t cube_tile(uint16_t line, uint8_t column)
{
    uint32_t bit = ((uint32_t)line * 19 + column) * CUBE_TILE_BITS;
    const uint8_t *p = cube_line_maps + (bit >> 3);
    uint32_t word = ((uint32_t)p[0] << 16) | ((uint16_t)p[1] << 8) | p[2];
    return (word >> (24 - CUBE_TILE_BITS - (bit & 7))) & ((1u << CUBE_TILE_BITS) - 1);
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
#ifndef GAME_CUBES_H
#define GAME_CUBES_H
#include "types.h"
#include "gen_data/cubes.h"
const CubeDef *cube_definition(uint8_t episode, uint8_t id);
const CubeDef *cube_available(uint8_t slot);
uint8_t cube_available_count(void);
uint16_t cube_tile(uint16_t line, uint8_t column);
#endif

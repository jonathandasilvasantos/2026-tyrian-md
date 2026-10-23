/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
/* Portable campaign checkpoints; no SRAM/ROM overlap on the SSF2 cart. */
#ifndef GAME_PASSWORD_H
#define GAME_PASSWORD_H
#include "types.h"
#define PASSWORD_LENGTH 48
extern const char password_alphabet[33];
/* Save at shops, as JE_saveGame: resume from script.save_section. */
void password_encode(char out[PASSWORD_LENGTH + 1]);
/* Validate everything before changing campaign state; returns 1 on success. */
uint8_t password_load(const char *text);
#endif

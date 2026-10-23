/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
/* types.h — fixed-width types for code shared by the ROM and the host. */
#ifndef GAME_TYPES_H
#define GAME_TYPES_H

/* SGDK has no libc stdint.h: its types.h maps the C99 names onto u8..u32. */
#ifdef SGDK_GCC
#include <types.h>
#else
#include <stdint.h>
#endif

#endif

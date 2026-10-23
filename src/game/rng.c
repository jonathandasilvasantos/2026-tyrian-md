/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * Portions adapted from OpenTyrian, Copyright (C) The OpenTyrian Development Team.
 * See LICENSE and NOTICE.
 */
/* rng.c — xorshift32.  OpenTyrian uses a Mersenne Twister; only the
 * distribution matters to gameplay, and MT costs far too much on a 68000. */
#include "rng.h"

uint32_t rng_state = 0x2545F491u;

void rng_seed(uint32_t seed)
{
    rng_state = seed ? seed : 0x2545F491u;
}

uint16_t rng16(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return (uint16_t)(x >> 16);
}

uint16_t rng_mod(uint16_t n)
{
    return (uint16_t)(rng16() % n);
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jonathan Santos and contributors.
 * See LICENSE and NOTICE.
 */
/* Compile the shipped SGDK math implementation with section granularity.
 * Its prebuilt archive puts all math entry points in one section: retaining
 * a fixed-point conversion also retains ~384 KB of unused log/sqrt tables.
 * Using the same read-only source lets --gc-sections discard those routines
 * and their tables. No implementation or numerical behavior is changed. */
#include "../../sgdk/src/maths.c"

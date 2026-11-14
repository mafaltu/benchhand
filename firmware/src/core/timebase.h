/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_TIMEBASE_H
#define CORE_TIMEBASE_H
#include <stdbool.h>
#include <stdint.h>

/* Millisecond tick counter; wraps at 2^32 (about 49.7 days). */
typedef uint32_t ms_t;

/* True when `now` has reached or passed `deadline`, wrap-safe for spans < 2^31 ms. */
static inline bool ms_reached(ms_t now, ms_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

#endif

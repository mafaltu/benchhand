/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_DEBOUNCE_H
#define CORE_DEBOUNCE_H
#include <stdbool.h>
#include <stdint.h>
#include "core/timebase.h"

/* Stable-level debouncer. `since` is the time of the first sample of the current raw level; the stable
 * level follows the raw level at the first update with now - since >= hold_ms. */
typedef struct {
    bool stable;
    bool last_raw;
    ms_t since;
} debounce_t;

void debounce_init(debounce_t *d, bool level, ms_t now);
/* Returns true when `stable` changed in this call. */
bool debounce_update(debounce_t *d, bool raw, ms_t now, ms_t hold_ms);

#endif

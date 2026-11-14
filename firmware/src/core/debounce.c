/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/debounce.h"

void debounce_init(debounce_t *d, bool level, ms_t now)
{
    d->stable = level;
    d->last_raw = level;
    d->since = now;
}

bool debounce_update(debounce_t *d, bool raw, ms_t now, ms_t hold_ms)
{
    if (raw != d->last_raw) {
        d->last_raw = raw;
        d->since = now;
    }
    if (raw != d->stable && (ms_t)(now - d->since) >= hold_ms) {
        d->stable = raw;
        return true;
    }
    return false;
}

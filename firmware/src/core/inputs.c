/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/inputs.h"

void inputs_init(inputs_t *in, const bool high[IN_N], ms_t now)
{
    for (uint8_t i = 0; i < IN_N; i++) {
        debounce_init(&in->d[i], high[i], now);
        in->rise[i] = 0;
        in->fall[i] = 0;
    }
}

void inputs_update(inputs_t *in, const bool high[IN_N], ms_t now)
{
    for (uint8_t i = 0; i < IN_N; i++) {
        if (debounce_update(&in->d[i], high[i], now, IN_DEBOUNCE_MS)) {
            if (in->d[i].stable) {
                in->rise[i]++;
            } else {
                in->fall[i]++;
            }
        }
    }
}

bool inputs_level(const inputs_t *in, uint8_t i0)
{
    return i0 < IN_N && in->d[i0].stable;
}

bool inputs_cond_met(const inputs_t *in, uint8_t i0, in_cond_t cond, uint32_t rise0, uint32_t fall0)
{
    if (i0 >= IN_N) {
        return false;
    }
    switch (cond) {
    case COND_HIGH: return in->d[i0].stable;
    case COND_LOW: return !in->d[i0].stable;
    case COND_RISE: return in->rise[i0] != rise0;
    case COND_FALL: return in->fall[i0] != fall0;
    default: return false;
    }
}

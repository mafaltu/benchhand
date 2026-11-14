/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/buttons.h"

void buttons_init(buttons_t *b, const bool pressed_raw[BTN_N], ms_t now)
{
    for (uint8_t i = 0; i < BTN_N; i++) {
        debounce_init(&b->d[i], pressed_raw[i], now);
        b->blocked[i] = (i < BTN_USB) && pressed_raw[i];
        b->presses[i] = 0;
    }
}

void buttons_update(buttons_t *b, const bool pressed_raw[BTN_N], ms_t now)
{
    for (uint8_t i = 0; i < BTN_N; i++) {
        if (debounce_update(&b->d[i], pressed_raw[i], now, BTN_DEBOUNCE_MS)) {
            if (b->d[i].stable) {
                b->presses[i]++;
            } else {
                b->blocked[i] = false;
            }
        }
    }
}

void buttons_rearm(buttons_t *b)
{
    for (uint8_t i = 0; i < BTN_USB; i++) {
        if (b->d[i].stable || b->d[i].last_raw) {
            b->blocked[i] = true;
        }
    }
}

uint8_t buttons_relay_held_mask(const buttons_t *b)
{
    uint8_t m = 0;
    for (uint8_t i = 0; i < BTN_USB; i++) {
        if (b->d[i].stable && !b->blocked[i]) {
            m |= (uint8_t)(1u << i);
        }
    }
    return m;
}

bool buttons_usb_held(const buttons_t *b)
{
    return b->d[BTN_USB].stable;
}

uint8_t buttons_raw_held_mask(const buttons_t *b)
{
    uint8_t m = 0;
    for (uint8_t i = 0; i < BTN_N; i++) {
        if (b->d[i].stable) {
            m |= (uint8_t)(1u << i);
        }
    }
    return m;
}

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_BUTTONS_H
#define CORE_BUTTONS_H
#include <stdbool.h>
#include <stdint.h>
#include "core/debounce.h"

#define BTN_N 6 /* 0..4 relays, 5 = USB */
#define BTN_USB 5
#define BTN_DEBOUNCE_MS 10u

typedef struct {
    debounce_t d[BTN_N];
    bool blocked[BTN_N];
    uint32_t presses[BTN_N];
} buttons_t;

/* pressed_raw is logical (true = pressed). A relay button held at init is blocked. */
void buttons_init(buttons_t *b, const bool pressed_raw[BTN_N], ms_t now);
void buttons_update(buttons_t *b, const bool pressed_raw[BTN_N], ms_t now);
/* Relay buttons whose debounced or last raw level is pressed become blocked until a debounced release. */
void buttons_rearm(buttons_t *b);
uint8_t buttons_relay_held_mask(const buttons_t *b); /* held and not blocked, bits 0..4 */
bool buttons_usb_held(const buttons_t *b);           /* debounced; never blocked (FW-27) */
uint8_t buttons_raw_held_mask(const buttons_t *b);   /* debounced, including blocked, bits 0..5 */

#endif

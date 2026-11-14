/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef TARGET_BOARD_H
#define TARGET_BOARD_H
#include <stdbool.h>

#include "core/app.h"

/* Board pin layer, USB-DEBUG-CTRL r1 (spec 2, onboarding pin map; design/pinmap_checks.md):
 *   RLY1..5      PA5, PA4, PA1, PA0, PB3   output, high = coil ON (100 k gate pull-down)
 *   LED_USB      PA7                       output, high = on
 *   PASS_VBUS_EN PB2, PASS_DATA_EN PA8     output, high = ON (hardware default OFF)
 *   BTN_RLY1..5  PB0, PA3, PC14, PB9, PB8  input, active low (100 k pull-up, RC)
 *   BTN_USB      PB1                       input, active low
 *   IN1..4       PA2, PB7, PB6, PB5        input, GPIO low = isolated input HIGH (22 k pull-up)
 *   PASS_PG_OK   PB4, PASS_OUT_OK PA15     input, high = OK (push-pull buffer U403), polled
 *   unused       PA6, PA9, PA10, PC15, PC6 analog; PA11/PA12 analog until USB takes them (Task 10)
 *   SWD          PA13, PA14                left in their reset (AF) state */

/* Spec 4.4: output data registers are written 0 before any pin becomes an output. Call first in main(). */
void board_init_safe(void);

/* Samples all inputs once and applies the active-low inversions. */
void board_read(app_in_t *in);

/* The single place that drives the output pins (one BSRR write per port). */
void board_write(const app_out_t *out);

/* 96-bit UID (UID_BASE words 0, 1, 2, each most significant nibble first) as 24 upper-case hex chars + NUL. */
void board_uid_hex(char out[25]);

#endif

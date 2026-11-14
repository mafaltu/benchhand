/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_INPUTS_H
#define CORE_INPUTS_H
#include <stdbool.h>
#include <stdint.h>
#include "core/debounce.h"
#include "core/protocol.h" /* in_cond_t */

#define IN_N 4
#define IN_DEBOUNCE_MS 2u /* = 3 equal 1 ms samples under the tick convention */

typedef struct {
    debounce_t d[IN_N];
    uint32_t rise[IN_N];
    uint32_t fall[IN_N];
} inputs_t;

void inputs_init(inputs_t *in, const bool high[IN_N], ms_t now);
void inputs_update(inputs_t *in, const bool high[IN_N], ms_t now); /* high = input voltage HIGH */
bool inputs_level(const inputs_t *in, uint8_t i0);                 /* i0 = 0..3 */
/* WAIT condition (SEQ WAIT step and standalone WAIT): HIGH/LOW on the debounced level, RISE/FALL when the edge
 * counter differs from the value captured when the wait started. False for i0 >= IN_N. */
bool inputs_cond_met(const inputs_t *in, uint8_t i0, in_cond_t cond, uint32_t rise0, uint32_t fall0);

#endif

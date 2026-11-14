/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_RELAYS_H
#define CORE_RELAYS_H
#include <stdbool.h>
#include <stdint.h>

#include "core/errors.h"
#include "core/timebase.h"

/* Relay arbiter (spec 4.1). The only module that decides whether a coil is energized:
 *   drive[n] = allowed AND (req[n] XOR btn_held[n]), with OFF->ON coil transitions staggered.
 * Requests are applied by direct calls from the single main-loop context; GPIO is written only
 * from relays_drive_mask() after relays_tick(). Channel bit 0 / ch0 = 0 is relay 1. */

#define RLY_N 5
#define RLY_MAX_ON_DEFAULT 60000u
#define RLY_STAGGER_MS 10u

typedef struct {
    bool req[RLY_N];        /* remote request (SET / PULSE / SEQ) */
    ms_t on_since[RLY_N];   /* time the request last went OFF->ON or was refreshed */
    bool has_pulse[RLY_N];
    ms_t pulse_dl[RLY_N];
    bool has_max[RLY_N];
    ms_t max_dl[RLY_N];
    ms_t max_on[RLY_N];     /* 0 = unlimited */
    bool drive[RLY_N];      /* coil state computed by the last relays_tick() */
    bool any_on_recent;     /* a coil turned ON less than RLY_STAGGER_MS ago */
    ms_t last_on;           /* time of that last OFF->ON coil transition */
} relays_t;

void relays_init(relays_t *r);                                  /* all OFF, max_on = 60000 */
void relays_set(relays_t *r, uint8_t mask, bool on, ms_t now);  /* table rows SET ON/OFF */
err_code_t relays_pulse(relays_t *r, uint8_t ch0, ms_t ms, ms_t now);  /* E_LIMIT if ms > max_on != 0 */
void relays_set_max_on(relays_t *r, uint8_t mask, ms_t v, ms_t now);   /* recompute from on_since */
void relays_tick(relays_t *r, ms_t now, bool allowed, uint8_t btn_held_mask);  /* expiry, XOR, stagger */
uint8_t relays_drive_mask(const relays_t *r);
uint8_t relays_req_mask(const relays_t *r);
/* ms to remote OFF; returns false when req OFF (left=0); *inf=true when ON without deadline */
bool relays_left(const relays_t *r, uint8_t ch0, ms_t now, uint32_t *left, bool *inf);
void relays_release_all(relays_t *r);                            /* safety event: req OFF, deadlines cleared */

#endif

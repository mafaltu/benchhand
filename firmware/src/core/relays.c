/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/relays.h"

/* Relay arbiter, spec 4.1 (request transition table) and 4.0 (safety release). */

static void req_off(relays_t *r, uint8_t n)
{
    r->req[n] = false;
    r->has_pulse[n] = false;
    r->has_max[n] = false;
}

/* Arm max_dl from on_since and max_on (none when max_on == 0). The comparison uses the elapsed time
 * since on_since, so a request held ON longer than 2^31 ms with MAX_ON 0 still expires at once when a
 * finite MAX_ON is set (on_since + v alone could look like a future deadline after wrap). */
static void arm_max(relays_t *r, uint8_t n, ms_t now)
{
    ms_t v = r->max_on[n];
    if (v == 0u) {
        r->has_max[n] = false;
        return;
    }
    r->has_max[n] = true;
    r->max_dl[n] = ((ms_t)(now - r->on_since[n]) >= v) ? now : (ms_t)(r->on_since[n] + v);
}

void relays_init(relays_t *r)
{
    for (uint8_t n = 0; n < RLY_N; n++) {
        req_off(r, n);
        r->on_since[n] = 0;
        r->pulse_dl[n] = 0;
        r->max_dl[n] = 0;
        r->max_on[n] = RLY_MAX_ON_DEFAULT;
        r->drive[n] = false;
    }
    r->any_on_recent = false;
    r->last_on = 0;
}

void relays_set(relays_t *r, uint8_t mask, bool on, ms_t now)
{
    for (uint8_t n = 0; n < RLY_N; n++) {
        if ((mask & (1u << n)) == 0u) {
            continue;
        }
        if (on) {
            /* OFF -> ON, or refresh of an ON request: same result (plain ON, pulse cleared) */
            r->req[n] = true;
            r->on_since[n] = now;
            r->has_pulse[n] = false;
            arm_max(r, n, now);
        } else {
            req_off(r, n);
        }
    }
}

err_code_t relays_pulse(relays_t *r, uint8_t ch0, ms_t ms, ms_t now)
{
    if (ch0 >= RLY_N) {
        return E_CHANNEL;
    }
    if (r->max_on[ch0] != 0u && ms > r->max_on[ch0]) {
        return E_LIMIT; /* request unchanged */
    }
    r->req[ch0] = true;
    r->on_since[ch0] = now;
    r->has_pulse[ch0] = true;
    r->pulse_dl[ch0] = now + ms;
    arm_max(r, ch0, now);
    return E_OK;
}

void relays_set_max_on(relays_t *r, uint8_t mask, ms_t v, ms_t now)
{
    for (uint8_t n = 0; n < RLY_N; n++) {
        if ((mask & (1u << n)) == 0u) {
            continue;
        }
        r->max_on[n] = v;
        if (r->req[n]) {
            /* max_dl = on_since + v; an already past deadline turns the request OFF in the next
             * relays_tick() (same ms under the tick convention). pulse_dl is kept. */
            arm_max(r, n, now);
        }
    }
}

void relays_tick(relays_t *r, ms_t now, bool allowed, uint8_t btn_held_mask)
{
    /* 1. deadlines: the request goes OFF at the earlier of pulse_dl / max_dl */
    for (uint8_t n = 0; n < RLY_N; n++) {
        if (!r->req[n]) {
            continue;
        }
        if ((r->has_pulse[n] && ms_reached(now, r->pulse_dl[n])) ||
            (r->has_max[n] && ms_reached(now, r->max_dl[n]))) {
            req_off(r, n);
        }
    }

    /* 2. stagger window; checked every tick, so the unsigned difference never wraps unseen */
    if (r->any_on_recent && (ms_t)(now - r->last_on) >= RLY_STAGGER_MS) {
        r->any_on_recent = false;
    }

    /* 3. drive = allowed AND (req XOR btn); OFF immediately, OFF->ON at most one per stagger
     *    interval, lowest channel first; a waiting coil is re-evaluated next tick. */
    for (uint8_t n = 0; n < RLY_N; n++) {
        bool btn = (btn_held_mask & (1u << n)) != 0u;
        bool want = allowed && (r->req[n] != btn);
        if (!want) {
            r->drive[n] = false;
        } else if (!r->drive[n] && !r->any_on_recent) {
            r->drive[n] = true;
            r->any_on_recent = true;
            r->last_on = now;
        }
    }
}

uint8_t relays_drive_mask(const relays_t *r)
{
    uint8_t m = 0;
    for (uint8_t n = 0; n < RLY_N; n++) {
        if (r->drive[n]) {
            m |= (uint8_t)(1u << n);
        }
    }
    return m;
}

uint8_t relays_req_mask(const relays_t *r)
{
    uint8_t m = 0;
    for (uint8_t n = 0; n < RLY_N; n++) {
        if (r->req[n]) {
            m |= (uint8_t)(1u << n);
        }
    }
    return m;
}

static uint32_t remaining(ms_t dl, ms_t now)
{
    return ms_reached(now, dl) ? 0u : (uint32_t)(dl - now);
}

bool relays_left(const relays_t *r, uint8_t ch0, ms_t now, uint32_t *left, bool *inf)
{
    *left = 0;
    *inf = false;
    if (ch0 >= RLY_N || !r->req[ch0]) {
        return false;
    }
    if (!r->has_pulse[ch0] && !r->has_max[ch0]) {
        *inf = true;
        return true;
    }
    uint32_t best = UINT32_MAX;
    if (r->has_pulse[ch0]) {
        uint32_t p = remaining(r->pulse_dl[ch0], now);
        best = p < best ? p : best;
    }
    if (r->has_max[ch0]) {
        uint32_t m = remaining(r->max_dl[ch0], now);
        best = m < best ? m : best;
    }
    *left = best;
    return true;
}

void relays_release_all(relays_t *r)
{
    /* Safety event (spec 4.0 step 2): every request and deadline cleared. Coils go OFF in the next
     * relays_tick(); a pending stagger slot disappears because its demand is gone. MAX_ON is kept. */
    for (uint8_t n = 0; n < RLY_N; n++) {
        req_off(r, n);
    }
}

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/passthrough.h"
#include <string.h>

/* Design spec 4.2. Safety invariants kept by this module:
 * - DATA_EN = 1 only in ON, which is reached only with VBUS_EN = 1 and the debounced pair 11 held for
 *   PT_SETTLE_MS; any debounced drop leaves ON in the same tick with DATA_EN = 0.
 * - Every DATA_EN 1 -> 0 happens at least one tick before VBUS_EN 1 -> 0 (SHUTDOWN / FAULT phase 0).
 * - VBUS_EN goes 0 -> 1 only on entry to POWERING, which needs: debounced status valid, OUT_OK debounced low
 *   (no external source on the output) and EN low for >= PT_EN_LOW_HOLD_MS (eFuse latch cleared, M16). */

static void debounce_sample(bool raw, bool *last_raw, uint8_t *cnt, bool *level, bool *valid)
{
    if (*cnt == 0u || raw != *last_raw) {
        *last_raw = raw;
        *cnt = 1u;
    } else if (*cnt < PT_STATUS_SAMPLES) {
        (*cnt)++;
    }
    if (*cnt >= PT_STATUS_SAMPLES) {
        *level = raw;
        *valid = true;
    }
}

static void enter(pt_t *p, pt_state_t st, ms_t now)
{
    p->st = st;
    p->t0 = now;
}

static void vbus_off(pt_t *p, ms_t now)
{
    if (p->vbus_en) {
        p->vbus_en = false;
        p->vbus_off_t = now;
        p->en_hold_done = false;
    }
}

/* Ordered off: data now, VBUS in the next tick (phase 0 -> 1). */
static void enter_shutdown(pt_t *p, pt_state_t st, ms_t now)
{
    p->data_en = false;
    p->shutdown_phase = p->vbus_en ? 0u : 1u;
    enter(p, st, now);
}

static void enter_fault(pt_t *p, ms_t now)
{
    p->cause = p->pg ? PT_FAULT_OUTPUT : PT_FAULT_EFUSE; /* "0x" -> efuse, "10" -> output (FW-19) */
    p->latched = true;
    enter_shutdown(p, PT_FAULT, now);
}

/* Try to start powering; only when no external source is on the output and the EN-low hold is done. */
static bool may_power(const pt_t *p)
{
    return p->pg_valid && p->out_valid && !p->out && p->en_hold_done;
}

static void enter_powering(pt_t *p, ms_t now)
{
    p->both_ok_seen = false;
    p->data_en = false;
    p->vbus_en = true;
    enter(p, PT_POWERING, now);
}

void pt_init(pt_t *p, ms_t now)
{
    memset(p, 0, sizeof *p);
    p->st = PT_OFF;
    p->usb_req = true;
    p->data_req = true;
    p->cause = PT_FAULT_NONE;
    /* EN is low from reset (spec 4.4), but a watchdog / software reset can reach the first tick only a few ms
     * after PB2 went low: measure the EN-low hold from init, as after any other VBUS_EN 1 -> 0 (Cx-I1). */
    p->vbus_off_t = now;
    p->en_hold_done = false;
}

void pt_set_usb(pt_t *p, bool on)
{
    p->usb_req = on;
    if (on) {
        p->latched = false;
        p->cause = PT_FAULT_NONE;
    }
}

void pt_set_data(pt_t *p, bool on)
{
    p->data_req = on;
}

void pt_disconnect_for(pt_t *p, ms_t ms, ms_t now)
{
    p->disc_active = true;
    p->disc_dl = now + ms;
}

void pt_cancel_disconnect(pt_t *p)
{
    p->disc_active = false;
}

void pt_tick(pt_t *p, ms_t now, bool pg_raw, bool out_raw, bool force_off)
{
    debounce_sample(pg_raw, &p->pg_raw, &p->pg_cnt, &p->pg, &p->pg_valid);
    debounce_sample(out_raw, &p->out_raw, &p->out_cnt, &p->out, &p->out_valid);

    if (!p->en_hold_done && (ms_t)(now - p->vbus_off_t) >= PT_EN_LOW_HOLD_MS) {
        p->en_hold_done = true;
    }
    if (p->disc_active && ms_reached(now, p->disc_dl)) {
        p->disc_active = false;
    }

    const bool want_power = p->usb_req && !force_off && !p->disc_active && !p->latched;
    const bool both_ok = p->pg_valid && p->out_valid && p->pg && p->out;

    switch (p->st) {
    case PT_OFF:
        if (p->latched) {
            enter_shutdown(p, PT_FAULT, now); /* restored latch: VBUS already 0 */
        } else if (want_power && p->pg_valid && p->out_valid) {
            if (p->out) {
                enter(p, PT_DISCHARGE, now);
            } else if (may_power(p)) {
                enter_powering(p, now);
            }
        }
        break;

    case PT_DISCHARGE:
        if (!want_power) {
            enter(p, PT_OFF, now);
        } else if (!p->out) {
            if (may_power(p)) {
                enter_powering(p, now);
            }
        } else if ((ms_t)(now - p->t0) >= PT_DISCHARGE_MS) {
            enter(p, PT_BLOCKED, now);
        }
        break;

    case PT_BLOCKED:
        if (!want_power || !p->out) {
            enter(p, PT_OFF, now);
        }
        break;

    case PT_POWERING:
        if (!want_power) {
            enter_shutdown(p, PT_SHUTDOWN, now);
        } else if (both_ok) {
            p->both_ok_seen = true;
            enter(p, PT_SETTLE, now);
        } else if ((ms_t)(now - p->t0) >= PT_TWAIT_MS) {
            /* FW-19: any pair other than 11 at timeout, start-up short included, is retried. DATA_EN is 0. */
            vbus_off(p, now);
            enter(p, PT_NO_INPUT, now);
        }
        break;

    case PT_NO_INPUT:
        if (!want_power || (ms_t)(now - p->t0) >= PT_RETRY_MS) {
            enter(p, PT_OFF, now);
        }
        break;

    case PT_SETTLE:
    case PT_POWER_ONLY:
    case PT_ON:
        /* A status drop after both_ok_seen is checked first: it latches even if an off request coincides. */
        if (!both_ok) {
            enter_fault(p, now);
        } else if (!want_power) {
            enter_shutdown(p, PT_SHUTDOWN, now);
        } else if (p->st == PT_SETTLE) {
            if ((ms_t)(now - p->t0) >= PT_SETTLE_MS) {
                p->data_en = p->data_req;
                enter(p, p->data_req ? PT_ON : PT_POWER_ONLY, now);
            }
        } else if (p->st == PT_POWER_ONLY && p->data_req) {
            p->data_en = true;
            enter(p, PT_ON, now);
        } else if (p->st == PT_ON && !p->data_req) {
            p->data_en = false;
            enter(p, PT_POWER_ONLY, now);
        }
        break;

    case PT_SHUTDOWN:
        /* Runs to completion even if want_power returns. */
        if (p->shutdown_phase == 0u) {
            vbus_off(p, now);
            p->shutdown_phase = 1u;
        } else if (p->en_hold_done) {
            if (p->latched) {
                enter_shutdown(p, PT_FAULT, now); /* restored latch (R2-M-1) */
            } else {
                enter(p, PT_OFF, now);
            }
        }
        break;

    case PT_FAULT:
        if (p->shutdown_phase == 0u) {
            vbus_off(p, now);
            p->shutdown_phase = 1u;
        } else if (!p->latched) {
            enter(p, PT_OFF, now); /* rearmed by USB ON; OFF still waits for the EN-low hold (M16) */
        }
        break;

    default: /* corrupted state: ordered off (data now, VBUS next tick) */
        enter_shutdown(p, p->latched ? PT_FAULT : PT_SHUTDOWN, now);
        break;
    }
}

bool pt_vbus_en(const pt_t *p)
{
    return p->vbus_en;
}

bool pt_data_en(const pt_t *p)
{
    return p->data_en;
}

bool pt_led(const pt_t *p, bool suspended)
{
    return p->st == PT_ON && !suspended;
}

pt_state_t pt_state(const pt_t *p)
{
    return p->st;
}

const char *pt_state_token(pt_state_t s)
{
    switch (s) {
    case PT_OFF: return "off";
    case PT_DISCHARGE: return "discharge";
    case PT_BLOCKED: return "blocked";
    case PT_POWERING: return "powering";
    case PT_NO_INPUT: return "no_input";
    case PT_SETTLE: return "powering";
    case PT_POWER_ONLY: return "power";
    case PT_ON: return "on";
    case PT_SHUTDOWN: return "off";
    case PT_FAULT: return "fault";
    default: return "off";
    }
}

pt_fault_t pt_fault(const pt_t *p)
{
    return p->cause;
}

const char *pt_fault_token(pt_fault_t f)
{
    switch (f) {
    case PT_FAULT_EFUSE: return "efuse";
    case PT_FAULT_OUTPUT: return "output";
    case PT_FAULT_NONE:
    default: return "none";
    }
}

bool pt_ext(const pt_t *p)
{
    return !p->vbus_en && p->out_valid && p->out;
}

bool pt_is_off(const pt_t *p)
{
    return !p->vbus_en && !p->data_en && p->st != PT_SHUTDOWN && p->en_hold_done;
}

pt_snapshot_t pt_snapshot(const pt_t *p)
{
    pt_snapshot_t s;
    s.usb_req = p->usb_req;
    s.data_req = p->data_req;
    s.latched = p->latched;
    s.cause = p->cause;
    return s;
}

void pt_restore(pt_t *p, const pt_snapshot_t *s)
{
    p->usb_req = s->usb_req;
    p->data_req = s->data_req;
    p->latched = s->latched;
    p->cause = s->latched ? s->cause : PT_FAULT_NONE;
}

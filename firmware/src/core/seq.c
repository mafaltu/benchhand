/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/seq.h"
#include <stdio.h>
#include <string.h>

/* Design spec 5.3: step completion, SEQ ownership, one cleanup path, MAX_ON rule. */

typedef enum { STEP_PENDING, STEP_DONE, STEP_FAIL } step_res_t;

void seq_init(seq_t *s)
{
    memset(s, 0, sizeof *s);
    s->status = SEQ_IDLE;
    s->reason = "";
}

err_code_t seq_validate(const seq_prog_t *p, const ms_t max_on[RLY_N])
{
    if (p->n == 0u || p->n > SEQ_MAX_STEPS) {
        return E_ARG;
    }
    for (uint8_t ch = 0; ch < RLY_N; ch++) {
        if (max_on[ch] == 0u) {
            continue; /* unlimited */
        }
        const uint8_t bit = (uint8_t)(1u << ch);
        bool on = false;
        uint64_t sum = 0; /* <= 16 x 3 600 000, but keep it overflow-proof */
        for (uint8_t i = 0; i < p->n; i++) {
            const seq_step_t *st = &p->step[i];
            if (st->kind == ST_SET && (st->mask & bit) != 0u) {
                on = st->on;
                sum = 0; /* ON (also a refresh) restarts on_since; OFF ends the span */
            } else if (on && (st->kind == ST_DELAY || st->kind == ST_WAIT)) {
                sum += st->ms;
                if (sum > max_on[ch]) {
                    return E_LIMIT;
                }
            }
        }
    }
    return E_OK;
}

void seq_start(seq_t *s, const seq_prog_t *p, const pt_t *pt, ms_t now)
{
    s->prog = *p;
    s->status = SEQ_RUNNING;
    s->idx = 0;
    s->t0 = now;
    s->entered = false;
    s->touched_mask = 0;
    s->pt_before = pt_snapshot(pt);
    s->rise0 = 0;
    s->fall0 = 0;
    s->reason = "";
}

/* Apply the step's action at the tick it is reached; its timer starts now. */
static void step_enter(seq_t *s, const seq_step_t *st, ms_t now, relays_t *r, pt_t *pt, const inputs_t *in)
{
    s->t0 = now;
    s->entered = true;
    switch (st->kind) {
    case ST_SET:
        relays_set(r, st->mask, st->on, now);
        s->touched_mask |= (uint8_t)(st->mask & 0x1Fu);
        break;
    case ST_USB:
        pt_set_usb(pt, st->on); /* ON also rearms a latched fault (restored on a failed end) */
        break;
    case ST_USB_DATA:
        pt_set_data(pt, st->on);
        break;
    case ST_WAIT:
        if (st->in < IN_N) {
            s->rise0 = in->rise[st->in];
            s->fall0 = in->fall[st->in];
        }
        break;
    case ST_DELAY:
    default:
        break;
    }
}

/* USB ON / USB DATA ON outcome. The state seen is the one left by the previous tick's pt_tick, so failure
 * states are only taken from the tick after the request (the reaching tick still shows the pre-request
 * state, e.g. a FAULT just rearmed). FAULT counts only while latched (a rearmed FAULT is leaving). */
static step_res_t usb_on_poll(seq_t *s, bool data_only, ms_t now, const pt_t *pt)
{
    const pt_state_t st = pt_state(pt);
    if (st == PT_ON || (!data_only && st == PT_POWER_ONLY)) {
        return STEP_DONE;
    }
    if (now != s->t0) {
        if (st == PT_NO_INPUT) {
            s->reason = "usb_no_input";
            return STEP_FAIL;
        }
        if (st == PT_BLOCKED) {
            s->reason = "usb_blocked";
            return STEP_FAIL;
        }
        if (st == PT_FAULT && pt_snapshot(pt).latched) {
            s->reason = "usb_fault";
            return STEP_FAIL;
        }
    }
    if ((ms_t)(now - s->t0) >= SEQ_USB_TIMEOUT_MS) {
        s->reason = "usb_timeout";
        return STEP_FAIL;
    }
    return STEP_PENDING;
}

static step_res_t step_poll(seq_t *s, const seq_step_t *st, ms_t now, const pt_t *pt, const inputs_t *in)
{
    switch (st->kind) {
    case ST_SET:
        return STEP_DONE; /* request applied; the arbiter tick of this ms drives the coil */
    case ST_DELAY:
        return ((ms_t)(now - s->t0) >= st->ms) ? STEP_DONE : STEP_PENDING;
    case ST_WAIT:
        if (inputs_cond_met(in, st->in, st->cond, s->rise0, s->fall0)) {
            return STEP_DONE;
        }
        if ((ms_t)(now - s->t0) >= st->ms) {
            s->reason = "timeout";
            return STEP_FAIL;
        }
        return STEP_PENDING;
    case ST_USB:
        if (st->on) {
            return usb_on_poll(s, false, now, pt);
        }
        if (!pt_vbus_en(pt) && !pt_data_en(pt)) {
            return STEP_DONE;
        }
        break;
    case ST_USB_DATA:
        if (st->on) {
            return usb_on_poll(s, true, now, pt);
        }
        if (!pt_data_en(pt)) {
            return STEP_DONE;
        }
        break;
    default:
        s->reason = "abort"; /* corrupted step: fail safe */
        return STEP_FAIL;
    }
    /* USB OFF / USB DATA OFF: physically off within 2 ticks; the timeout is only a backstop. */
    if ((ms_t)(now - s->t0) >= SEQ_USB_TIMEOUT_MS) {
        s->reason = "usb_timeout";
        return STEP_FAIL;
    }
    return STEP_PENDING;
}

void seq_tick(seq_t *s, ms_t now, relays_t *r, pt_t *pt, const inputs_t *in)
{
    if (s->status != SEQ_RUNNING) {
        return;
    }
    if (s->idx == 0u && !s->entered && now == s->t0) {
        return; /* seq_start only arms; steps run from T0 + 1 */
    }
    /* Bounded: each pass either returns or advances idx. */
    while (s->idx < s->prog.n && s->idx < SEQ_MAX_STEPS) {
        const seq_step_t *st = &s->prog.step[s->idx];
        if (!s->entered) {
            step_enter(s, st, now, r, pt, in);
        }
        step_res_t res = step_poll(s, st, now, pt, in);
        if (res == STEP_PENDING) {
            return;
        }
        if (res == STEP_FAIL) {
            const char *why = s->reason;
            seq_abort(s, why, r, pt);
            return;
        }
        s->idx++;
        s->entered = false;
    }
    s->status = SEQ_DONE;
}

void seq_abort(seq_t *s, const char *reason, relays_t *r, pt_t *pt)
{
    if (s->status != SEQ_RUNNING) {
        return;
    }
    s->status = SEQ_ABORTED;
    s->reason = reason != NULL ? reason : "abort";

    /* 1. every relay the SEQ touched: request OFF, deadlines cleared */
    if (s->touched_mask != 0u) {
        relays_set(r, s->touched_mask, false, 0);
    }

    /* 2. requests and latch from before the SEQ. A fault latched now (during the SEQ) is kept: restoring
     *    never bypasses FAULT, and only a standalone USB ON clears a latch. */
    pt_snapshot_t restore = s->pt_before;
    const pt_snapshot_t cur = pt_snapshot(pt);
    if (cur.latched) {
        restore.latched = true;
        restore.cause = cur.cause;
    }
    pt_restore(pt, &restore);
}

bool seq_running(const seq_t *s)
{
    return s->status == SEQ_RUNNING;
}

uint32_t seq_delay_ms(const seq_prog_t *p)
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < p->n && i < SEQ_MAX_STEPS; i++) {
        if (p->step[i].kind == ST_DELAY) {
            sum += p->step[i].ms;
        }
    }
    return sum;
}

bool seq_async(const seq_prog_t *p)
{
    for (uint8_t i = 0; i < p->n && i < SEQ_MAX_STEPS; i++) {
        step_kind_t k = p->step[i].kind;
        if (k == ST_USB || k == ST_USB_DATA || k == ST_WAIT) {
            return true;
        }
    }
    return false;
}

int seq_status_token(const seq_t *s, char *buf, size_t n)
{
    switch (s->status) {
    case SEQ_RUNNING: {
        unsigned i = (unsigned)s->idx + 1u;
        if (i > s->prog.n) {
            i = s->prog.n;
        }
        return snprintf(buf, n, "running:%u/%u", i, (unsigned)s->prog.n);
    }
    case SEQ_DONE: return snprintf(buf, n, "done");
    case SEQ_ABORTED: return snprintf(buf, n, "aborted:%s", s->reason != NULL ? s->reason : "abort");
    case SEQ_IDLE:
    default: return snprintf(buf, n, "idle");
    }
}

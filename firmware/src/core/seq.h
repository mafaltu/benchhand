/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_SEQ_H
#define CORE_SEQ_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/errors.h"
#include "core/inputs.h"
#include "core/passthrough.h"
#include "core/protocol.h"
#include "core/relays.h"
#include "core/timebase.h"

/* SEQ engine (design spec 5.3, FW-15). Runs a validated sequence of SET / USB / USB DATA / DELAY / WAIT steps
 * in the background through the relay arbiter and the passthrough state machine. Pure module.
 *
 * Timing (tick convention): seq_start(now = T0) only arms; steps run from seq_tick(T0 + 1). SET steps chain
 * within one tick; a DELAY / WAIT / USB step starts its timer at the tick it is reached. seq_tick runs before
 * relays_tick and pt_tick in the main loop, so a USB step sees the passthrough state of the previous tick.
 *
 * Every end other than success goes through seq_abort(): touched relays OFF (deadlines cleared), then
 * usb_req / data_req / fault latch restored from the snapshot taken at seq_start. A fault latched during the
 * SEQ is kept (restoring never bypasses FAULT). */

#define SEQ_USB_TIMEOUT_MS 1000u /* USB step without outcome -> reason usb_timeout */

typedef enum { SEQ_IDLE, SEQ_RUNNING, SEQ_DONE, SEQ_ABORTED } seq_status_t;

typedef struct {
    seq_prog_t prog;
    seq_status_t status;
    uint8_t idx;            /* current step, 0-based */
    ms_t t0;                /* seq_start time until step 0 is reached, then the current step's start tick */
    uint8_t touched_mask;   /* relays changed by a SET step of this SEQ */
    pt_snapshot_t pt_before;
    uint32_t rise0, fall0;  /* input edge counters when the current WAIT step was reached */
    const char *reason;     /* static string; set when ABORTED */
    bool entered;           /* current step reached (its action applied, timer started) */
} seq_t;

void seq_init(seq_t *s);
/* E_ARG for an empty / oversized program; E_LIMIT when DELAY ms plus full WAIT timeouts between a relay's ON
 * and its next OFF (or the program end) exceed that relay's max_on != 0. A repeated ON restarts the count,
 * as relays_set() restarts on_since. */
err_code_t seq_validate(const seq_prog_t *p, const ms_t max_on[RLY_N]);
void seq_start(seq_t *s, const seq_prog_t *p, const pt_t *pt, ms_t now); /* takes the pt snapshot */
void seq_tick(seq_t *s, ms_t now, relays_t *r, pt_t *pt, const inputs_t *in);
/* One cleanup path (SEQ ABORT, step failure, safety event). No-op unless RUNNING. `reason` must be static. */
void seq_abort(seq_t *s, const char *reason, relays_t *r, pt_t *pt);
bool seq_running(const seq_t *s);
uint32_t seq_delay_ms(const seq_prog_t *p); /* sum of DELAY steps */
bool seq_async(const seq_prog_t *p);        /* contains a USB, USB DATA or WAIT step */
/* STATUS token: "idle" | "running:<i>/<n>" (i 1-based) | "done" | "aborted:<reason>"; snprintf semantics. */
int seq_status_token(const seq_t *s, char *buf, size_t n);

#endif

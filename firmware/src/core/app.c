/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/app.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core/build_info.h"
#include "core/help_text.h"

/* Design spec 4.0 (safety_release), 4.1 (allowed / startup_inhibit), 4.3 step 1-2 (suspend, logic part),
 * 4.6 (REBOOT / BOOTLOADER ordering), 5 (protocol). */

static const char HINT_BUSY[] = "SEQ running, send SEQ ABORT or wait";
static const char HINT_NOT_READY[] = "relays are enabled after USB configuration";
static const char HINT_PULSE_LIMIT[] = "PULSE longer than MAX_ON of this relay, raise MAX_ON or shorten the pulse";
static const char HINT_SEQ_LIMIT[] = "SEQ keeps a relay ON longer than its MAX_ON, raise MAX_ON or shorten DELAY/WAIT";
static const char HINT_SEQ_ARG[] = "SEQ needs 1-16 steps separated by ';'";
static const char HINT_WAIT_TIMEOUT[] = "input condition not met within the WAIT time";
static const char HINT_TOO_LONG[] = "line longer than 320 characters, the whole line was ignored";
static const char HINT_TOPIC[] = "no such HELP topic, send HELP for the command list";

/* ---------- reply staging ---------- */

static bool out_empty(const app_t *a)
{
    return a->out_pos >= a->out_len;
}

/* Append one formatted line + CRLF. The buffer is empty whenever a reply starts and every reply line is
 * bounded (STATUS worst case about 430 bytes), so this never truncates; if it would, the line is cut but
 * still ends with CRLF. */
static void stage(app_t *a, const char *fmt, ...)
{
    size_t room = (size_t)APP_OUT_BUF - a->out_len;
    if (room < 3u) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(a->out + a->out_len, room - 2u, fmt, ap);
    va_end(ap);
    if (n < 0) {
        n = 0;
    }
    if ((size_t)n > room - 3u) {
        n = (int)(room - 3u);
    }
    a->out_len = (uint16_t)(a->out_len + (uint16_t)n);
    a->out[a->out_len++] = '\r';
    a->out[a->out_len++] = '\n';
}

static void reply_ok(app_t *a)
{
    stage(a, "OK");
}

static void reply_err(app_t *a, err_code_t code, const char *hint)
{
    stage(a, "ERR %d %s: %s", (int)code, err_reason(code), hint);
}

/* One output pass: stage at most one HELP line, then write what the TX FIFO takes. tx_space 0 for
 * APP_TX_STALL_MS drops the rest of the pending reply (Review Focus 2). Never blocks. */
static void pump(app_t *a, ms_t now)
{
    if (out_empty(a)) {
        a->out_len = 0;
        a->out_pos = 0;
        if (a->help_active) {
            const char *l = help_line(a->help_topic, a->help_idx);
            if (l != NULL) {
                a->help_idx++;
                stage(a, "%s", l);
            } else {
                a->help_active = false;
                reply_ok(a);
            }
        }
    }
    if (out_empty(a)) {
        a->stall_active = false;
        return;
    }
    size_t rem = (size_t)(a->out_len - a->out_pos);
    size_t space = a->env.tx_space(a->env.ctx);
    if (space > 0u) {
        size_t n = space < rem ? space : rem;
        a->env.tx_write(a->env.ctx, a->out + a->out_pos, n);
        a->out_pos = (uint16_t)(a->out_pos + (uint16_t)n);
        a->stall_active = false;
        if (out_empty(a)) {
            a->out_len = 0;
            a->out_pos = 0;
        }
        return;
    }
    if (!a->stall_active) {
        a->stall_active = true;
        a->stall_t0 = now;
    } else if ((ms_t)(now - a->stall_t0) >= APP_TX_STALL_MS) {
        a->out_len = 0;
        a->out_pos = 0;
        a->help_active = false;
        a->stall_active = false;
    }
}

/* ---------- state helpers ---------- */

static bool relays_allowed(const app_t *a)
{
    return a->configured && !a->suspended && !a->startup_inhibit && a->closing == APP_RUN;
}

/* Spec 4.0: one idempotent cleanup for suspend, bus reset, deconfiguration, REBOOT, BOOTLOADER.
 * pt_off: the event also takes the passthrough off (suspend, REBOOT, BOOTLOADER); the force_off itself comes
 * from `suspended` / `closing` in app_tick; here the USB DISCONNECT timer is ended (FW-09). */
static void safety_release(app_t *a, const char *reason, bool pt_off)
{
    seq_abort(&a->seq, reason, &a->relays, &a->pt);
    relays_release_all(&a->relays);
    buttons_rearm(&a->buttons);
    a->startup_inhibit = true;
    if (pt_off) {
        pt_cancel_disconnect(&a->pt);
    }
    if (a->wait_active) {
        a->wait_active = false;
        stage(a, "ERR %d %s: %s, state was reset", (int)E_ABORTED, err_reason(E_ABORTED), reason);
    }
}

/* Cl-m2: BUS_RESET / UNCONFIGURED: drop a partial input line and any pending or streaming reply, so nothing
 * from before the event is glued to the next command or reaches the next reader. */
static void discard_io(app_t *a)
{
    linebuf_init(&a->lb);
    a->out_len = 0;
    a->out_pos = 0;
    a->help_active = false;
    a->stall_active = false;
}

static void cancel_stop(app_t *a)
{
    a->stop_armed = false;
    a->stop_ready = false;
}

static void list5(char *buf, size_t n, const uint32_t v[5])
{
    snprintf(buf, n, "%lu,%lu,%lu,%lu,%lu", (unsigned long)v[0], (unsigned long)v[1], (unsigned long)v[2],
             (unsigned long)v[3], (unsigned long)v[4]);
}

static void bits(char *buf, uint8_t mask, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        buf[2 * i] = (mask & (1u << i)) ? '1' : '0';
        buf[2 * i + 1] = (i + 1u < n) ? ',' : '\0';
    }
}

/* ---------- replies ---------- */

static void reply_status(app_t *a, ms_t now)
{
    char rly[10], req[10], btn[10], in[8], presses[60], left[60], seq[32];
    uint32_t pr[RLY_N];
    uint8_t in_mask = 0;
    size_t lp = 0;

    bits(rly, a->drive_mask, RLY_N);
    bits(req, relays_req_mask(&a->relays), RLY_N);
    bits(btn, (uint8_t)(buttons_raw_held_mask(&a->buttons) & 0x1Fu), RLY_N);
    for (uint8_t i = 0; i < IN_N; i++) {
        in_mask |= (uint8_t)(inputs_level(&a->inputs, i) ? (1u << i) : 0u);
    }
    bits(in, in_mask, IN_N);
    for (uint8_t i = 0; i < RLY_N; i++) {
        pr[i] = a->buttons.presses[i];
    }
    list5(presses, sizeof presses, pr);
    for (uint8_t i = 0; i < RLY_N; i++) {
        uint32_t l;
        bool inf;
        relays_left(&a->relays, i, now, &l, &inf);
        if (inf) {
            lp += (size_t)snprintf(left + lp, sizeof left - lp, "%sinf", i ? "," : "");
        } else {
            lp += (size_t)snprintf(left + lp, sizeof left - lp, "%s%lu", i ? "," : "", (unsigned long)l);
        }
    }
    seq_status_token(&a->seq, seq, sizeof seq);
    const pt_snapshot_t ps = pt_snapshot(&a->pt);

    stage(a,
          "OK rly=%s req=%s btn=%s presses=%s left=%s in=%s usb=%s usb_req=%d data_req=%d ext=%d usb_btn=%d "
          "usb_presses=%lu fault=%s seq=%s ready=%d boot=%lu reset=%.8s uptime=%lu suspends=%lu lp_err=%d "
          "optr_err=%d",
          rly, req, btn, presses, left, in, pt_state_token(pt_state(&a->pt)), ps.usb_req, ps.data_req,
          pt_ext(&a->pt), buttons_usb_held(&a->buttons), (unsigned long)a->buttons.presses[BTN_USB],
          pt_fault_token(pt_fault(&a->pt)), seq, relays_allowed(a), (unsigned long)a->env.boot_count,
          a->env.reset_token != NULL ? a->env.reset_token : "pin", (unsigned long)(ms_t)(now - a->t_init),
          (unsigned long)a->suspends, a->lp_err, a->env.optr_err);
}

static void reply_id(app_t *a)
{
    stage(a, "OK model=BenchHand hw=r1 fw=%.16s proto=" PROTO_VERSION " sn=%.24s",
          a->env.fw_version != NULL ? a->env.fw_version : FW_VERSION,
          a->env.serial != NULL ? a->env.serial : "000000000000000000000000");
}

static void reply_max_on(app_t *a)
{
    char buf[60];
    uint32_t v[RLY_N];
    for (uint8_t i = 0; i < RLY_N; i++) {
        v[i] = a->relays.max_on[i];
    }
    list5(buf, sizeof buf, v);
    stage(a, "OK max_on=%s", buf);
}

static void reply_input(app_t *a)
{
    const inputs_t *in = &a->inputs;
    stage(a, "OK in=%d,%d,%d,%d rise=%lu,%lu,%lu,%lu fall=%lu,%lu,%lu,%lu", inputs_level(in, 0),
          inputs_level(in, 1), inputs_level(in, 2), inputs_level(in, 3), (unsigned long)in->rise[0],
          (unsigned long)in->rise[1], (unsigned long)in->rise[2], (unsigned long)in->rise[3],
          (unsigned long)in->fall[0], (unsigned long)in->fall[1], (unsigned long)in->fall[2],
          (unsigned long)in->fall[3]);
}

/* ---------- commands ---------- */

static bool seq_has_set_on(const seq_prog_t *p)
{
    for (uint8_t i = 0; i < p->n && i < SEQ_MAX_STEPS; i++) {
        if (p->step[i].kind == ST_SET && p->step[i].on) {
            return true;
        }
    }
    return false;
}

static uint8_t mask_to_ch0(uint8_t mask)
{
    uint8_t ch = 0;
    while (ch < RLY_N && (mask & (1u << ch)) == 0u) {
        ch++;
    }
    return ch;
}

static void start_wait(app_t *a, const cmd_t *c, ms_t now)
{
    a->wait_in = c->in;
    a->wait_cond = c->cond;
    a->wait_ms = c->ms;
    a->wait_t0 = now;
    a->wait_rise0 = c->in < IN_N ? a->inputs.rise[c->in] : 0u;
    a->wait_fall0 = c->in < IN_N ? a->inputs.fall[c->in] : 0u;
    if (inputs_cond_met(&a->inputs, a->wait_in, a->wait_cond, a->wait_rise0, a->wait_fall0)) {
        stage(a, "OK t=0");
    } else {
        a->wait_active = true;
    }
}

static void wait_progress(app_t *a, ms_t now)
{
    if (!a->wait_active) {
        return;
    }
    if (inputs_cond_met(&a->inputs, a->wait_in, a->wait_cond, a->wait_rise0, a->wait_fall0)) {
        a->wait_active = false;
        stage(a, "OK t=%lu", (unsigned long)(ms_t)(now - a->wait_t0));
    } else if ((ms_t)(now - a->wait_t0) >= a->wait_ms) {
        a->wait_active = false;
        reply_err(a, E_TIMEOUT, HINT_WAIT_TIMEOUT);
    }
}

/* Output-changing commands: refused while a SEQ runs (spec 5.3 SEQ ownership, ruling M7). */
static void exec_output_cmd(app_t *a, const cmd_t *c, ms_t now)
{
    if (seq_running(&a->seq)) {
        reply_err(a, E_BUSY, HINT_BUSY);
        return;
    }
    const bool ready = relays_allowed(a);
    switch (c->kind) {
    case C_SET:
        if (c->on && !ready) {
            reply_err(a, E_NOT_READY, HINT_NOT_READY);
            return;
        }
        relays_set(&a->relays, c->mask, c->on, now);
        break;
    case C_PULSE:
        if (!ready) {
            reply_err(a, E_NOT_READY, HINT_NOT_READY);
            return;
        }
        if (relays_pulse(&a->relays, mask_to_ch0(c->mask), c->ms, now) != E_OK) {
            reply_err(a, E_LIMIT, HINT_PULSE_LIMIT); /* request unchanged */
            return;
        }
        break;
    case C_MAX_ON:
        relays_set_max_on(&a->relays, c->mask, c->ms, now);
        break;
    case C_WAIT:
        start_wait(a, c, now);
        return;
    case C_USB:
        pt_set_usb(&a->pt, c->on);
        break;
    case C_USB_DATA:
        pt_set_data(&a->pt, c->on);
        break;
    case C_USB_DISCONNECT:
        pt_disconnect_for(&a->pt, c->ms, now);
        break;
    case C_SEQ: {
        if (seq_has_set_on(&c->seq) && !ready) {
            reply_err(a, E_NOT_READY, HINT_NOT_READY);
            return;
        }
        err_code_t e = seq_validate(&c->seq, a->relays.max_on);
        if (e != E_OK) {
            reply_err(a, e, e == E_LIMIT ? HINT_SEQ_LIMIT : HINT_SEQ_ARG);
            return;
        }
        seq_start(&a->seq, &c->seq, &a->pt, now);
        stage(a, "OK steps=%u delay_ms=%lu async=%d", (unsigned)c->seq.n, (unsigned long)seq_delay_ms(&c->seq),
              seq_async(&c->seq));
        return;
    }
    case C_REBOOT:
    case C_BOOTLOADER:
        /* Spec 4.6: release, passthrough off; OK only after pt_is_off() (app_tick); request after the OK. */
        a->closing = (c->kind == C_REBOOT) ? APP_REQ_REBOOT : APP_REQ_BOOTLOADER;
        a->closing_ok_staged = false;
        a->closing_done = false;
        safety_release(a, c->kind == C_REBOOT ? "reboot" : "bootloader", true);
        return;
    default:
        return;
    }
    reply_ok(a);
}

static void exec_line(app_t *a, const char *line, ms_t now)
{
    perr_t e;
    cmd_t *c = &a->cmd;
    if (!proto_parse(line, c, &e)) {
        reply_err(a, e.code, e.hint);
        return;
    }
    switch (c->kind) {
    case C_IGNORE:
        return; /* echoed OK / ERR / # line or blank (FW-23) */
    case C_HELP:
        if (!help_topic_exists(c->topic)) {
            reply_err(a, E_ARG, HINT_TOPIC);
            return;
        }
        memcpy(a->help_topic, c->topic, sizeof a->help_topic);
        a->help_topic[sizeof a->help_topic - 1u] = '\0';
        a->help_idx = 0;
        a->help_active = true;
        return;
    case C_ID: reply_id(a); return;
    case C_STATUS: reply_status(a, now); return;
    case C_MAX_ON_Q: reply_max_on(a); return;
    case C_INPUT_Q: reply_input(a); return;
    case C_SEQ_ABORT:
        if (seq_running(&a->seq)) {
            seq_abort(&a->seq, "abort", &a->relays, &a->pt);
            reply_ok(a);
        } else {
            stage(a, "OK seq=idle");
        }
        return;
    default:
        exec_output_cmd(a, c, now);
        return;
    }
}

/* ---------- public ---------- */

void app_init(app_t *a, const app_env_t *env, const app_in_t *in, ms_t now)
{
    memset(a, 0, sizeof *a);
    a->env = *env;
    a->t_init = now;
    linebuf_init(&a->lb);
    relays_init(&a->relays);
    buttons_init(&a->buttons, in->btn_pressed, now);
    inputs_init(&a->inputs, in->in_high, now);
    pt_init(&a->pt, now);
    seq_init(&a->seq);
    a->startup_inhibit = true;
    a->closing = APP_RUN;
}

bool app_can_accept_rx(const app_t *a)
{
    return a->closing == APP_RUN && !a->wait_active && !a->help_active && out_empty(a);
}

void app_rx_byte(app_t *a, char c, ms_t now)
{
    if (!app_can_accept_rx(a)) {
        return; /* caller contract: bytes are held back while a reply is pending */
    }
    const char *line = NULL;
    switch (linebuf_push(&a->lb, c, &line)) {
    case LB_LINE: exec_line(a, line, now); break;
    case LB_TOO_LONG: reply_err(a, E_TOO_LONG, HINT_TOO_LONG); break;
    case LB_NONE:
    default: return;
    }
    pump(a, now); /* reply at once when the FIFO has room */
}

void app_usb_event(app_t *a, usb_ev_t ev, ms_t now)
{
    switch (ev) {
    case USBEV_CONFIGURED:
        a->configured = true;
        a->suspended = false;
        a->resume_hold = false;
        cancel_stop(a);
        break;
    case USBEV_UNCONFIGURED:
        a->configured = false;
        discard_io(a);
        safety_release(a, "unconfigured", false); /* may stage the ERR 11 reply of an aborted WAIT */
        break;
    case USBEV_BUS_RESET:
        a->configured = false;
        a->suspended = false;
        a->resume_hold = false;
        cancel_stop(a);
        discard_io(a);
        safety_release(a, "bus_reset", false);
        break;
    case USBEV_SUSPEND:
        a->suspended = true;
        a->suspends++;
        safety_release(a, "suspend", true);
        a->stop_armed = true;
        a->stop_ready = false;
        a->stop_t0 = now;
        break;
    case USBEV_RESUME:
        /* Cl-m1: the FSDEV raises RESUME on every wake-up, also on bus noise (RM0490 Table 162: "go back in
         * Suspend mode"), which is followed by a new SUSPEND ~3 ms later. Keep the passthrough forced off for
         * APP_RESUME_CONFIRM_MS so such a wake never pulses VBUS; a real resume (K state >= 20 ms, then SOFs)
         * is not followed by a SUSPEND, and CONFIGURED / BUS_RESET end the hold at once. */
        a->suspended = false;
        a->resume_hold = true;
        a->resume_t0 = now;
        cancel_stop(a);
        break;
    default:
        break;
    }
}

void app_tick(app_t *a, ms_t now, const app_in_t *in, app_out_t *out)
{
    buttons_update(&a->buttons, in->btn_pressed, now);
    inputs_update(&a->inputs, in->in_high, now);

    if (a->configured && !a->suspended && a->closing == APP_RUN) {
        a->startup_inhibit = false;
    }
    const bool allowed = relays_allowed(a);
    if (allowed && !a->allowed_prev) {
        buttons_rearm(&a->buttons); /* a button held when relays become allowed needs a new press (M-4) */
    }
    a->allowed_prev = allowed;

    seq_tick(&a->seq, now, &a->relays, &a->pt, &a->inputs);
    relays_tick(&a->relays, now, allowed, buttons_relay_held_mask(&a->buttons));
    a->drive_mask = relays_drive_mask(&a->relays);

    if (a->resume_hold && (ms_t)(now - a->resume_t0) >= APP_RESUME_CONFIRM_MS) {
        a->resume_hold = false;
    }
    /* FW-05 / FW-27: the USB button forces off even when held at startup; suspend, an unconfirmed resume and
     * closing force off. */
    const bool force_off =
        buttons_usb_held(&a->buttons) || a->suspended || a->resume_hold || a->closing != APP_RUN;
    pt_tick(&a->pt, now, in->pg_ok, in->out_ok, force_off);

    if (a->suspended && a->stop_armed && !a->stop_ready && !a->env.optr_err &&
        (pt_is_off(&a->pt) || (ms_t)(now - a->stop_t0) >= APP_STOP_WAIT_MS)) {
        a->stop_ready = true;
    }
    if (a->closing != APP_RUN && !a->closing_ok_staged && pt_is_off(&a->pt)) {
        a->closing_ok_staged = true;
        reply_ok(a);
    }
    wait_progress(a, now);
    pump(a, now);
    if (a->closing_ok_staged && out_empty(a) && !a->help_active) {
        a->closing_done = true;
    }

    out->relay_mask = a->drive_mask;
    out->vbus_en = pt_vbus_en(&a->pt);
    out->data_en = pt_data_en(&a->pt);
    out->led_usb = pt_led(&a->pt, a->suspended);
}

app_request_t app_request(const app_t *a)
{
    if (a->closing != APP_RUN && a->closing_done) {
        return a->closing;
    }
    if (a->suspended && a->stop_ready && !a->env.optr_err) {
        return APP_REQ_STOP;
    }
    return APP_RUN;
}

void app_stop_done(app_t *a)
{
    cancel_stop(a);
}

void app_set_lp_err(app_t *a)
{
    a->lp_err = true;
}

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_APP_H
#define CORE_APP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/buttons.h"
#include "core/inputs.h"
#include "core/linebuf.h"
#include "core/passthrough.h"
#include "core/protocol.h"
#include "core/relays.h"
#include "core/seq.h"
#include "core/timebase.h"

/* App layer (design spec 4.0, 4.1, 4.3 logic part, 5): command dispatcher, replies, STATUS, WAIT,
 * safety_release, USB events, requests to the main loop. Pure module; everything runs in main-loop context.
 *
 * Timing (tick convention): a line is executed inside the app_rx_byte() call that completes it, with the
 * `now` of the last tick; outputs change only in app_tick(). Replies go through a 512-byte staging buffer
 * written with env->tx_space / tx_write; HELP is staged one line per pass. A new line is accepted only when
 * the previous reply is fully written (app_can_accept_rx). The caller must not feed bytes while
 * app_can_accept_rx() is false (they are ignored).
 * Time contract: the `now` values passed to app_tick / app_usb_event / app_rx_byte are non-decreasing across all
 * calls (wrap-safe unsigned differences). An app_usb_event time may be newer than the last app_tick's `now`; event
 * timers (SUSPEND stop wait, RESUME hold) count from the event's own time, so the caller stamps events with the
 * tick at delivery. */

#define APP_OUT_BUF       512u
#define APP_TX_STALL_MS   1000u /* tx_space 0 this long -> rest of the pending reply dropped (Review Focus 2) */
#define APP_STOP_WAIT_MS  20u   /* SUSPEND: STOP request after pt_is_off() or this long (spec 4.3 step 2) */
#define APP_RESUME_CONFIRM_MS 10u /* RESUME: passthrough stays forced off this long (noise wake, Cl-m1) */

typedef struct {
    bool btn_pressed[BTN_N]; /* logical: true = pressed */
    bool in_high[IN_N];      /* true = HIGH voltage on the isolated input */
    bool pg_ok, out_ok;      /* passthrough status, true = OK / output high */
} app_in_t;

typedef struct {
    uint8_t relay_mask; /* bit0 = RLY1 */
    bool vbus_en, data_en, led_usb;
} app_out_t;

typedef enum { USBEV_CONFIGURED, USBEV_UNCONFIGURED, USBEV_BUS_RESET, USBEV_SUSPEND, USBEV_RESUME } usb_ev_t;

typedef struct {
    size_t (*tx_space)(void *ctx); /* free bytes in the CDC TX FIFO */
    void (*tx_write)(void *ctx, const char *s, size_t n);
    void *ctx;
    const char *serial;      /* 24 upper-case hex chars */
    const char *fw_version;  /* NULL -> FW_VERSION (build_info.h) */
    const char *reset_token; /* one of por|obl|iwdg|wwdg|lpwr|sw|pin */
    uint32_t boot_count;
    bool optr_err;           /* IWDG_STOP option byte not 0: never request Stop */
} app_env_t;

typedef enum { APP_RUN, APP_REQ_REBOOT, APP_REQ_BOOTLOADER, APP_REQ_STOP } app_request_t;

/* Private; use the functions below. Defined here only so the caller can allocate it statically. */
struct app {
    app_env_t env;
    ms_t t_init;
    linebuf_t lb;
    cmd_t cmd; /* parse scratch (holds a whole SEQ program; kept off the stack) */
    relays_t relays;
    buttons_t buttons;
    inputs_t inputs;
    pt_t pt;
    seq_t seq;
    bool configured, suspended, startup_inhibit, allowed_prev;
    uint8_t drive_mask; /* coil drive of the last tick (STATUS rly=) */
    uint32_t suspends;
    bool lp_err;
    /* STOP request (one-shot per SUSPEND) */
    bool stop_armed, stop_ready;
    ms_t stop_t0;
    /* RESUME not yet confirmed: passthrough forced off until resume_t0 + APP_RESUME_CONFIRM_MS */
    bool resume_hold;
    ms_t resume_t0;
    /* REBOOT / BOOTLOADER: APP_RUN = not closing */
    app_request_t closing;
    bool closing_ok_staged, closing_done;
    /* standalone WAIT */
    bool wait_active;
    uint8_t wait_in;
    in_cond_t wait_cond;
    ms_t wait_ms, wait_t0;
    uint32_t wait_rise0, wait_fall0;
    /* reply staging */
    char out[APP_OUT_BUF];
    uint16_t out_len, out_pos;
    bool help_active;
    char help_topic[16];
    uint16_t help_idx;
    bool stall_active;
    ms_t stall_t0;
};
typedef struct app app_t;

/* env is copied. in: raw levels at init (relay buttons held now stay blocked until released). */
void app_init(app_t *a, const app_env_t *env, const app_in_t *in, ms_t now);
bool app_can_accept_rx(const app_t *a); /* false while a reply is pending, a WAIT blocks or closing */
void app_rx_byte(app_t *a, char c, ms_t now);
void app_usb_event(app_t *a, usb_ev_t ev, ms_t now); /* main-loop context only, never from an ISR */
void app_tick(app_t *a, ms_t now, const app_in_t *in, app_out_t *out); /* every 1 ms */
app_request_t app_request(const app_t *a); /* main loop acts when outputs are safe */
void app_stop_done(app_t *a);              /* main loop: STOP handled (woke up); one-shot (ruling C5) */
void app_set_lp_err(app_t *a);             /* sticky lp_err=1 (Task 11) */

#endif

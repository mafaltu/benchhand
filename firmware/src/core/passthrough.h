/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_PASSTHROUGH_H
#define CORE_PASSTHROUGH_H
#include <stdbool.h>
#include <stdint.h>
#include "core/timebase.h"

/* USB passthrough state machine (design spec 4.2; FW-05, FW-19, FW-21). Pure module: drives the
 * TPS259474L eFuse enable (PASS_VBUS_EN) and the TS3USB221E data switch enable (PASS_DATA_EN) from the
 * PG_OK / OUT_OK status inputs. pt_tick() is called every 1 ms; it makes at most one state transition. */

#define PT_TWAIT_MS        100u  /* POWERING: wait for PG_OK & OUT_OK */
#define PT_SETTLE_MS       10u   /* SETTLE: both OK this long before data may connect */
#define PT_DISCHARGE_MS    2000u /* DISCHARGE: wait for OUT_OK low before BLOCKED */
#define PT_RETRY_MS        2000u /* NO_INPUT: retry interval */
#define PT_EN_LOW_HOLD_MS  10u   /* EN low at least this long (clears the eFuse latch) */
#define PT_STATUS_SAMPLES  3u    /* status debounce: equal consecutive 1 ms samples */

typedef enum {
    PT_OFF,
    PT_DISCHARGE,
    PT_BLOCKED,
    PT_POWERING,
    PT_NO_INPUT,
    PT_SETTLE,
    PT_POWER_ONLY,
    PT_ON,
    PT_SHUTDOWN,
    PT_FAULT
} pt_state_t;

typedef enum { PT_FAULT_NONE, PT_FAULT_EFUSE, PT_FAULT_OUTPUT } pt_fault_t;

typedef struct {
    bool usb_req, data_req, latched;
    pt_fault_t cause;
} pt_snapshot_t;

/* Private; use the accessors. */
typedef struct {
    pt_state_t st;
    ms_t t0;               /* entry time of the current timed state */
    bool usb_req, data_req, latched, both_ok_seen;
    pt_fault_t cause;
    bool vbus_en, data_en;
    uint8_t pg_cnt, out_cnt;     /* consecutive equal raw samples (saturating) */
    bool pg, out;                /* debounced levels */
    bool pg_raw, out_raw;        /* last raw sample */
    bool pg_valid, out_valid;    /* debounced level known (PT_STATUS_SAMPLES seen at least once) */
    bool disc_active;
    ms_t disc_dl;
    uint8_t shutdown_phase;      /* SHUTDOWN / FAULT: 0 = data off, VBUS still on; 1 = VBUS off */
    ms_t vbus_off_t;             /* time VBUS_EN last went 1 -> 0 */
    bool en_hold_done;           /* VBUS_EN has been 0 for >= PT_EN_LOW_HOLD_MS (counted from init too) */
} pt_t;

/* usb_req = data_req = true (FW-21). The EN-low hold starts at `now`: VBUS_EN cannot rise before
 * now + PT_EN_LOW_HOLD_MS, whatever happened before the reset. */
void pt_init(pt_t *p, ms_t now);
void pt_set_usb(pt_t *p, bool on); /* on: also clears latch + cause (rearm) */
void pt_set_data(pt_t *p, bool on);
void pt_disconnect_for(pt_t *p, ms_t ms, ms_t now);
void pt_cancel_disconnect(pt_t *p);
/* force_off: USB button held, suspend, REBOOT, BOOTLOADER in progress. */
void pt_tick(pt_t *p, ms_t now, bool pg_raw, bool out_raw, bool force_off);

bool pt_vbus_en(const pt_t *p);
bool pt_data_en(const pt_t *p);
bool pt_led(const pt_t *p, bool suspended);
pt_state_t pt_state(const pt_t *p);
const char *pt_state_token(pt_state_t s); /* STATUS usb= token (spec 5.5) */
pt_fault_t pt_fault(const pt_t *p);
const char *pt_fault_token(pt_fault_t f); /* "none", "efuse", "output" */
bool pt_ext(const pt_t *p);    /* VBUS_EN == 0 && debounced OUT_OK high */
bool pt_is_off(const pt_t *p); /* both EN 0, not SHUTDOWN, EN-low hold completed */
pt_snapshot_t pt_snapshot(const pt_t *p);
void pt_restore(pt_t *p, const pt_snapshot_t *s); /* latched = 1 forces SHUTDOWN -> FAULT */

#endif

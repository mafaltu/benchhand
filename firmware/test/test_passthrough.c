/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/passthrough.h"
#include "minitest.h"

/* Tick convention (global constraints): init (see begin()), ticks at 1, 2, 3, ...; a command issued after
 * tick T acts in tick T+1; force_off and the raw status levels are inputs of the tick itself.
 * Status debounce: PT_STATUS_SAMPLES (3) equal samples, so a raw level first sampled at t is debounced at t+2. */

/* ---- safety invariants, checked after every tick by step() ---- */
static struct {
    bool prev_vbus, prev_data;
    bool fall_valid;
    ms_t last_fall;
    bool data_seen;   /* data_en was 1 at some tick since the last begin() / clear_data_seen() */
} inv;

/* pt_init at t_init; the invariant checker counts the EN-low hold from init (EN is low from reset). */
static void begin_at(pt_t *p, ms_t t_init)
{
    pt_init(p, t_init);
    memset(&inv, 0, sizeof inv);
    inv.fall_valid = true;
    inv.last_fall = t_init;
}

/* Fixture: the module is initialised PT_EN_LOW_HOLD_MS before tick 0, so the init EN-low hold is complete at
 * tick 1 and the scenario timings below stay as written. The init hold itself: test_init_holds_en_low. */
static void begin(pt_t *p)
{
    begin_at(p, (ms_t)0 - PT_EN_LOW_HOLD_MS);
}

static void step(pt_t *p, ms_t t, bool pg, bool out, bool force)
{
    pt_tick(p, t, pg, out, force);
    bool v = pt_vbus_en(p);
    bool d = pt_data_en(p);
    /* Data only with VBUS on and debounced pair 11. */
    if (d) {
        CHECK(v);
        CHECK(p->pg && p->out && p->pg_valid && p->out_valid);
        inv.data_seen = true;
    }
    /* Data goes off at least one tick before VBUS. */
    if (inv.prev_vbus && !v) {
        CHECK(!inv.prev_data);
        inv.fall_valid = true;
        inv.last_fall = t;
    }
    /* VBUS rises only with known, debounced OUT_OK low and after the EN-low hold. */
    if (!inv.prev_vbus && v) {
        CHECK(p->out_valid && !p->out);
        if (inv.fall_valid) {
            CHECK((ms_t)(t - inv.last_fall) >= PT_EN_LOW_HOLD_MS);
        }
    }
    inv.prev_vbus = v;
    inv.prev_data = d;
}

/* Tick every ms from `from` to `to` inclusive (wrap-safe). */
static void run(pt_t *p, ms_t from, ms_t to, bool pg, bool out, bool force)
{
    for (ms_t t = from;; t++) {
        step(p, t, pg, out, force);
        if (t == to) {
            break;
        }
    }
}

/* Tick from..to; true if the state equalled `st` after every tick. */
static bool run_in(pt_t *p, ms_t from, ms_t to, bool pg, bool out, bool force, pt_state_t st)
{
    bool ok = true;
    for (ms_t t = from;; t++) {
        step(p, t, pg, out, force);
        if (pt_state(p) != st) {
            ok = false;
        }
        if (t == to) {
            break;
        }
    }
    return ok;
}

/* Cold connect: status 00 from t=1, 11 from t=20 -> ON at 32. */
static void connect(pt_t *p)
{
    begin(p);
    run(p, 1, 19, false, false, false);
    run(p, 20, 32, true, true, false);
}

TEST(test_cold_connect)
{
    pt_t p;
    begin(&p);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    CHECK(!pt_vbus_en(&p) && !pt_data_en(&p));
    run(&p, 1, 2, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    CHECK(!pt_vbus_en(&p));
    step(&p, 3, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
    CHECK(pt_vbus_en(&p));
    CHECK(!pt_data_en(&p));
    CHECK_EQ_STR(pt_state_token(pt_state(&p)), "powering");
    CHECK(run_in(&p, 4, 19, false, false, false, PT_POWERING));
    CHECK(run_in(&p, 20, 21, true, true, false, PT_POWERING));
    step(&p, 22, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_SETTLE);
    CHECK_EQ_STR(pt_state_token(pt_state(&p)), "powering");
    CHECK(run_in(&p, 23, 31, true, true, false, PT_SETTLE));
    CHECK(!pt_data_en(&p));
    CHECK(!pt_led(&p, false));
    step(&p, 32, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_ON);
    CHECK(pt_data_en(&p) && pt_vbus_en(&p));
    CHECK(pt_led(&p, false));
    CHECK(!pt_led(&p, true));
    CHECK_EQ_STR(pt_state_token(pt_state(&p)), "on");
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_NONE);
    CHECK(!pt_ext(&p));
}

TEST(test_ext_at_reset_never_powers)
{
    pt_t p;
    begin(&p);
    for (ms_t t = 1; t <= 3000; t++) {
        step(&p, t, false, true, false);
        CHECK(!pt_vbus_en(&p));
        if (t <= 2) {
            CHECK_EQ_INT(pt_state(&p), PT_OFF);
        } else if (t == 3) {
            CHECK_EQ_INT(pt_state(&p), PT_DISCHARGE);
            CHECK(pt_ext(&p));
            CHECK_EQ_STR(pt_state_token(pt_state(&p)), "discharge");
        } else if (t == 2002) {
            CHECK_EQ_INT(pt_state(&p), PT_DISCHARGE);
        } else if (t == 2003) {
            CHECK_EQ_INT(pt_state(&p), PT_BLOCKED);
            CHECK_EQ_STR(pt_state_token(pt_state(&p)), "blocked");
        }
    }
    CHECK_EQ_INT(pt_state(&p), PT_BLOCKED);
    CHECK(pt_ext(&p));
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_NONE);
}

TEST(test_power_only_and_back)
{
    pt_t p;
    connect(&p);
    run(&p, 33, 40, true, true, false);
    pt_set_data(&p, false);
    step(&p, 41, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWER_ONLY);
    CHECK(!pt_data_en(&p));
    CHECK(pt_vbus_en(&p));
    CHECK(!pt_led(&p, false));
    CHECK_EQ_STR(pt_state_token(pt_state(&p)), "power");
    CHECK(run_in(&p, 42, 50, true, true, false, PT_POWER_ONLY));
    pt_set_data(&p, true);
    step(&p, 51, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_ON);
    CHECK(pt_data_en(&p));

    /* data_req OFF before connect: POWER_ONLY at 32, data never 1. */
    begin(&p);
    pt_set_data(&p, false);
    run(&p, 1, 19, false, false, false);
    run(&p, 20, 31, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_SETTLE);
    step(&p, 32, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWER_ONLY);
    CHECK(pt_vbus_en(&p));
    run(&p, 33, 500, true, true, false);
    CHECK(!inv.data_seen);
}

TEST(test_no_input_retry)
{
    pt_t p;
    begin(&p);
    run(&p, 1, 2, false, false, false);
    step(&p, 3, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
    CHECK(run_in(&p, 4, 102, false, false, false, PT_POWERING));
    step(&p, 103, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_NO_INPUT);
    CHECK(!pt_vbus_en(&p));
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_NONE);
    CHECK(!pt_ext(&p));
    CHECK_EQ_STR(pt_state_token(pt_state(&p)), "no_input");
    CHECK(run_in(&p, 104, 2102, false, false, false, PT_NO_INPUT));
    step(&p, 2103, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    CHECK(!pt_vbus_en(&p));
    step(&p, 2104, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
    CHECK(pt_vbus_en(&p));
}

TEST(test_pg_only_pulse_is_no_input)
{
    pt_t p;
    /* 2 ms PG-only pulse (below the debounce). */
    begin(&p);
    run(&p, 1, 49, false, false, false);
    run(&p, 50, 51, true, false, false);
    CHECK(run_in(&p, 52, 102, false, false, false, PT_POWERING));
    step(&p, 103, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_NO_INPUT);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_NONE);

    /* Debounced PG-only (10) and OUT-only (01) pulses during POWERING: still NO_INPUT, not FAULT. */
    begin(&p);
    run(&p, 1, 29, false, false, false);
    run(&p, 30, 39, true, false, false);
    run(&p, 40, 49, false, true, false);
    CHECK(run_in(&p, 50, 102, false, false, false, PT_POWERING));
    step(&p, 103, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_NO_INPUT);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_NONE);
    CHECK(!pt_snapshot(&p).latched);
}

TEST(test_startup_short_retried)
{
    pt_t p;
    begin(&p);
    run(&p, 1, 3, true, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
    CHECK(run_in(&p, 4, 102, true, false, false, PT_POWERING));
    step(&p, 103, true, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_NO_INPUT);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_NONE);
    CHECK(!pt_snapshot(&p).latched);
    run(&p, 104, 2103, true, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    step(&p, 2104, true, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
    step(&p, 2204, true, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_NO_INPUT); /* second attempt, timer from 2104 */
    CHECK(!pt_snapshot(&p).latched);
}

TEST(test_powering_not_want_power_shuts_down)
{
    pt_t p;
    begin(&p);
    run(&p, 1, 10, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
    pt_set_usb(&p, false);
    step(&p, 11, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_SHUTDOWN);
    CHECK_EQ_STR(pt_state_token(pt_state(&p)), "off");
    CHECK(!pt_data_en(&p));
    CHECK(pt_vbus_en(&p));
    CHECK(!pt_is_off(&p));
    step(&p, 12, false, false, false);
    CHECK(!pt_vbus_en(&p));
    CHECK_EQ_INT(pt_state(&p), PT_SHUTDOWN);
    CHECK(!pt_is_off(&p));
    CHECK(run_in(&p, 13, 21, false, false, false, PT_SHUTDOWN));
    CHECK(!pt_is_off(&p));
    step(&p, 22, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    CHECK(pt_is_off(&p));
    CHECK(run_in(&p, 23, 3000, false, false, false, PT_OFF));

    /* SHUTDOWN completes even if want_power returns in between. */
    begin(&p);
    run(&p, 1, 10, false, false, false);
    pt_set_usb(&p, false);
    step(&p, 11, false, false, false);
    pt_set_usb(&p, true);
    step(&p, 12, false, false, false);
    CHECK(!pt_vbus_en(&p));
    CHECK(run_in(&p, 13, 21, false, false, false, PT_SHUTDOWN));
    CHECK(!pt_vbus_en(&p));
    step(&p, 22, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    step(&p, 23, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
}

TEST(test_settle_drop_faults)
{
    pt_t p;
    /* PG drops in SETTLE -> efuse. */
    begin(&p);
    run(&p, 1, 19, false, false, false);
    run(&p, 20, 24, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_SETTLE);
    CHECK(run_in(&p, 25, 26, false, true, false, PT_SETTLE));
    step(&p, 27, false, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_FAULT);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_EFUSE);
    CHECK_EQ_STR(pt_fault_token(pt_fault(&p)), "efuse");
    CHECK_EQ_STR(pt_state_token(pt_state(&p)), "fault");
    CHECK(!pt_data_en(&p));
    step(&p, 28, false, true, false);
    CHECK(!pt_vbus_en(&p));
    CHECK(run_in(&p, 29, 1000, false, false, false, PT_FAULT));
    CHECK(!inv.data_seen);

    /* OUT drops in SETTLE with PG OK -> output. */
    begin(&p);
    run(&p, 1, 19, false, false, false);
    run(&p, 20, 24, true, true, false);
    CHECK(run_in(&p, 25, 26, true, false, false, PT_SETTLE));
    step(&p, 27, true, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_FAULT);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_OUTPUT);
    CHECK_EQ_STR(pt_fault_token(pt_fault(&p)), "output");
    step(&p, 28, true, false, false);
    CHECK(!pt_vbus_en(&p));
    CHECK(!inv.data_seen);
}

TEST(test_post_on_drop_latches_efuse)
{
    pt_t p;
    connect(&p);
    run(&p, 33, 99, true, true, false);
    CHECK(run_in(&p, 100, 101, false, true, false, PT_ON));
    step(&p, 102, false, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_FAULT);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_EFUSE);
    CHECK(!pt_data_en(&p));
    CHECK(pt_vbus_en(&p));
    step(&p, 103, false, true, false);
    CHECK(!pt_vbus_en(&p));
    CHECK(pt_snapshot(&p).latched);
    /* stays FAULT for 60 000 ticks, also through data_req changes */
    pt_set_data(&p, false);
    pt_set_data(&p, true);
    CHECK(run_in(&p, 104, 60103, false, false, false, PT_FAULT));
    CHECK(!pt_vbus_en(&p));
    CHECK(pt_is_off(&p));
    /* USB OFF does not clear the latch */
    pt_set_usb(&p, false);
    CHECK(run_in(&p, 60104, 60200, false, false, false, PT_FAULT));
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_EFUSE);
    /* USB ON rearms -> OFF -> reconnect */
    pt_set_usb(&p, true);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_NONE);
    step(&p, 60201, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    CHECK_EQ_STR(pt_fault_token(pt_fault(&p)), "none");
    step(&p, 60202, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
    run(&p, 60203, 60203, true, true, false);
    run(&p, 60204, 60215, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_ON);
}

TEST(test_post_on_out_drop_is_output_fault)
{
    pt_t p;
    connect(&p);
    run(&p, 33, 99, true, true, false);
    CHECK(run_in(&p, 100, 101, true, false, false, PT_ON));
    step(&p, 102, true, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_FAULT);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_OUTPUT);
    CHECK(!pt_data_en(&p));
    step(&p, 103, true, false, false);
    CHECK(!pt_vbus_en(&p));

    /* Same in POWER_ONLY. */
    connect(&p);
    pt_set_data(&p, false);
    run(&p, 33, 99, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWER_ONLY);
    run(&p, 100, 102, true, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_FAULT);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_OUTPUT);
}

TEST(test_single_glitch_ignored)
{
    pt_t p;
    connect(&p);
    run(&p, 33, 99, true, true, false);
    CHECK(run_in(&p, 100, 101, false, true, false, PT_ON));
    CHECK(run_in(&p, 102, 200, true, true, false, PT_ON));
    CHECK(run_in(&p, 201, 202, true, false, false, PT_ON));
    CHECK(run_in(&p, 203, 300, true, true, false, PT_ON));
    CHECK(pt_data_en(&p));
}

TEST(test_ext_power_discharge_then_connect)
{
    pt_t p;
    begin(&p);
    run(&p, 1, 2, false, true, false);
    step(&p, 3, false, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_DISCHARGE);
    CHECK(pt_ext(&p));
    for (ms_t t = 4; t <= 499; t++) {
        step(&p, t, false, true, false);
        CHECK(!pt_vbus_en(&p));
        CHECK(pt_ext(&p));
    }
    CHECK(run_in(&p, 500, 501, false, false, false, PT_DISCHARGE));
    step(&p, 502, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
    CHECK(pt_vbus_en(&p));
    CHECK(!pt_ext(&p));
}

TEST(test_ext_power_blocked)
{
    pt_t p;
    begin(&p);
    run(&p, 1, 2002, false, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_DISCHARGE);
    step(&p, 2003, false, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_BLOCKED);
    CHECK(pt_ext(&p));
    CHECK(run_in(&p, 2004, 2499, false, true, false, PT_BLOCKED));
    CHECK(run_in(&p, 2500, 2501, false, false, false, PT_BLOCKED));
    step(&p, 2502, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    CHECK(!pt_ext(&p));
    step(&p, 2503, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
}

TEST(test_not_want_power_exits_to_off)
{
    pt_t p;
    /* DISCHARGE */
    begin(&p);
    run(&p, 1, 10, false, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_DISCHARGE);
    pt_set_usb(&p, false);
    step(&p, 11, false, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    CHECK(run_in(&p, 12, 3000, false, true, false, PT_OFF));
    CHECK(pt_ext(&p)); /* still reported in OFF */

    /* BLOCKED */
    begin(&p);
    run(&p, 1, 2100, false, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_BLOCKED);
    pt_set_usb(&p, false);
    step(&p, 2101, false, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);

    /* NO_INPUT */
    begin(&p);
    run(&p, 1, 200, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_NO_INPUT);
    pt_set_usb(&p, false);
    step(&p, 201, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    CHECK(run_in(&p, 202, 5000, false, false, false, PT_OFF));
    CHECK(!inv.prev_vbus);

    /* force_off in DISCHARGE -> OFF too */
    begin(&p);
    run(&p, 1, 10, false, true, false);
    step(&p, 11, false, true, true);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
}

TEST(test_ext_reported_in_fault_and_no_input)
{
    pt_t p;
    /* NO_INPUT: out raw 1 from k = 201 -> ext at 203. */
    begin(&p);
    run(&p, 1, 200, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_NO_INPUT);
    step(&p, 201, false, true, false);
    CHECK(!pt_ext(&p));
    step(&p, 202, false, true, false);
    CHECK(!pt_ext(&p));
    step(&p, 203, false, true, false);
    CHECK(pt_ext(&p));
    CHECK_EQ_INT(pt_state(&p), PT_NO_INPUT);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_NONE);

    /* FAULT */
    connect(&p);
    run(&p, 33, 99, true, true, false);
    run(&p, 100, 200, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_FAULT);
    step(&p, 201, false, true, false);
    CHECK(!pt_ext(&p));
    step(&p, 202, false, true, false);
    CHECK(!pt_ext(&p));
    step(&p, 203, false, true, false);
    CHECK(pt_ext(&p));
    CHECK_EQ_INT(pt_state(&p), PT_FAULT);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_EFUSE); /* ext is not a fault cause */
}

TEST(test_force_off_button)
{
    pt_t p;
    connect(&p);
    run(&p, 33, 99, true, true, false);
    step(&p, 100, true, true, true);
    CHECK_EQ_INT(pt_state(&p), PT_SHUTDOWN);
    CHECK(!pt_data_en(&p));
    CHECK(pt_vbus_en(&p));
    step(&p, 101, false, false, true);
    CHECK(!pt_vbus_en(&p));
    run(&p, 102, 104, false, false, true);
    run(&p, 105, 109, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_SHUTDOWN);
    step(&p, 110, false, false, false);
    CHECK(!pt_is_off(&p));
    CHECK(!pt_vbus_en(&p));
    step(&p, 111, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    CHECK(pt_is_off(&p));
    CHECK(!pt_vbus_en(&p));
    step(&p, 112, false, false, false);
    CHECK(pt_vbus_en(&p));
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);

    /* With usb_req false, force_off (pressed / released) never turns it on. */
    begin(&p);
    pt_set_usb(&p, false);
    for (ms_t t = 1; t <= 3000; t++) {
        step(&p, t, false, false, (t % 7u) < 3u);
        CHECK(!pt_vbus_en(&p));
    }

    /* Held from reset: stays OFF while held; connect starts in the tick it is released. */
    begin(&p);
    CHECK(run_in(&p, 1, 500, false, false, true, PT_OFF));
    step(&p, 501, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
}

TEST(test_disconnect_for)
{
    pt_t p;
    connect(&p);
    run(&p, 33, 100, true, true, false);
    pt_disconnect_for(&p, 1000, 100);
    step(&p, 101, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_SHUTDOWN);
    CHECK(!pt_data_en(&p));
    step(&p, 102, false, false, false);
    CHECK(!pt_vbus_en(&p));
    CHECK(run_in(&p, 103, 111, false, false, false, PT_SHUTDOWN));
    step(&p, 112, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    CHECK(run_in(&p, 113, 1099, false, false, false, PT_OFF));
    step(&p, 1100, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);

    /* Cancel ends the disconnect early. */
    connect(&p);
    run(&p, 33, 100, true, true, false);
    pt_disconnect_for(&p, 1000, 100);
    run(&p, 101, 200, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    pt_cancel_disconnect(&p);
    step(&p, 201, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
}

TEST(test_restore_latched_snapshot)
{
    pt_t p;
    connect(&p);
    run(&p, 33, 99, true, true, false);
    run(&p, 100, 102, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_FAULT);
    run(&p, 103, 200, false, false, false);
    inv.data_seen = false;
    pt_snapshot_t s = pt_snapshot(&p);
    CHECK(s.latched);
    CHECK_EQ_INT(s.cause, PT_FAULT_EFUSE);
    CHECK(s.usb_req && s.data_req);

    pt_set_usb(&p, true); /* e.g. a SEQ's USB ON */
    step(&p, 201, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    step(&p, 202, true, true, false); /* debounced OUT still low */
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
    step(&p, 203, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_SETTLE);
    run(&p, 204, 205, true, true, false);

    /* restore after tick R = 205: SHUTDOWN from R+1 = 206, VBUS off at 207, FAULT at R+12 = 217. */
    pt_restore(&p, &s);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_EFUSE);
    step(&p, 206, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_SHUTDOWN);
    CHECK(pt_vbus_en(&p));
    step(&p, 207, true, true, false);
    CHECK(!pt_vbus_en(&p));
    CHECK(run_in(&p, 208, 216, false, false, false, PT_SHUTDOWN));
    step(&p, 217, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_FAULT);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_EFUSE);
    CHECK(run_in(&p, 218, 5000, false, false, false, PT_FAULT));
    CHECK(!pt_vbus_en(&p));
    CHECK(!inv.data_seen);

    /* Restoring an unlatched snapshot just restores the requests. */
    begin(&p);
    pt_snapshot_t u = {false, false, false, PT_FAULT_NONE};
    pt_restore(&p, &u);
    CHECK(run_in(&p, 1, 300, false, false, false, PT_OFF));
    s = pt_snapshot(&p);
    CHECK(!s.usb_req && !s.data_req && !s.latched);

    /* Restoring latched while OFF goes to FAULT without ever powering. */
    begin(&p);
    pt_snapshot_t l = {true, true, true, PT_FAULT_OUTPUT};
    pt_restore(&p, &l);
    run(&p, 1, 300, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_FAULT);
    CHECK_EQ_INT(pt_fault(&p), PT_FAULT_OUTPUT);
    CHECK(!inv.prev_vbus);
}

TEST(test_rearm_respects_en_low_hold)
{
    pt_t p;
    connect(&p);
    run(&p, 33, 99, true, true, false);
    run(&p, 100, 101, false, false, false);
    step(&p, 102, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_FAULT);
    CHECK(pt_vbus_en(&p));
    pt_set_usb(&p, true); /* rearm immediately */
    step(&p, 103, false, false, false);
    CHECK(!pt_vbus_en(&p)); /* fell at 103 */
    step(&p, 104, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    CHECK(!pt_is_off(&p)); /* EN-low hold not done */
    for (ms_t t = 105; t <= 112; t++) {
        step(&p, t, false, false, false);
        CHECK(!pt_vbus_en(&p));
        CHECK_EQ_INT(pt_state(&p), PT_OFF);
    }
    step(&p, 113, false, false, false); /* 113 - 103 = 10 */
    CHECK(pt_vbus_en(&p));
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
}

TEST(test_pt_timers_across_wrap)
{
    pt_t p;
    const ms_t base = 0xFFFFFBFDu;
    const ms_t d0 = 0xFFFFFC00u;
    begin(&p);
    run(&p, base + 1u, base + 2u, false, true, false);
    step(&p, d0, false, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_DISCHARGE);
    CHECK(run_in(&p, d0 + 1u, d0 + 1999u, false, true, false, PT_DISCHARGE));
    step(&p, d0 + 2000u, false, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_BLOCKED);

    /* NO_INPUT retry and disconnect deadline across the wrap. */
    begin(&p);
    run(&p, 0xFFFFFFF0u, 0xFFFFFFF2u, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING); /* t0 = 0xFFFFFFF2 */
    CHECK(run_in(&p, 0xFFFFFFF3u, 0x55u, false, false, false, PT_POWERING));
    step(&p, 0x56u, false, false, false); /* 0xFFFFFFF2 + 100 */
    CHECK_EQ_INT(pt_state(&p), PT_NO_INPUT);
    pt_disconnect_for(&p, 3000, 0x56u);
    run(&p, 0x57u, 0x56u + 2999u, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_OFF);
    step(&p, 0x56u + 3000u, false, false, false);
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);
}

/* Cx-I1: the EN-low hold is measured from init (a watchdog / software reset can reach the first ticks a few
 * ms after PB2 went low), even when the status is valid at once. */
TEST(test_init_holds_en_low)
{
    pt_t p;
    begin_at(&p, 0);
    for (ms_t t = 1; t < PT_EN_LOW_HOLD_MS; t++) {
        step(&p, t, false, false, false);
        CHECK(!pt_vbus_en(&p));
        CHECK(!pt_is_off(&p)); /* EN-low hold not done yet */
        CHECK_EQ_INT(pt_state(&p), PT_OFF);
    }
    step(&p, PT_EN_LOW_HOLD_MS, false, false, false);
    CHECK(pt_vbus_en(&p));
    CHECK_EQ_INT(pt_state(&p), PT_POWERING);

    /* Same across the 32-bit wrap. */
    const ms_t t0 = 0xFFFFFFFAu;
    begin_at(&p, t0);
    for (ms_t t = t0 + 1u; t != (ms_t)(t0 + PT_EN_LOW_HOLD_MS); t++) {
        step(&p, t, false, false, false);
        CHECK(!pt_vbus_en(&p));
    }
    step(&p, t0 + PT_EN_LOW_HOLD_MS, false, false, false);
    CHECK(pt_vbus_en(&p));
}

/* Task 6 ruling / Cl-m7: a debounced status drop in the same tick as an off request (pt_set_usb(false) or
 * force_off) latches FAULT with the right cause; ordered shutdown (data now, VBUS next tick). */
static void drop_and_off_variant(bool pg_after, bool use_force, pt_fault_t want)
{
    pt_t p;
    connect(&p);
    run(&p, 33, 99, true, true, false);
    CHECK_EQ_INT(pt_state(&p), PT_ON);
    run(&p, 100, 101, pg_after, false, false); /* raw drop from 100, debounced at 102 */
    CHECK_EQ_INT(pt_state(&p), PT_ON);
    if (!use_force) {
        pt_set_usb(&p, false); /* after tick 101: acts in tick 102, together with the debounced drop */
    }
    step(&p, 102, pg_after, false, use_force);
    CHECK_EQ_INT(pt_state(&p), PT_FAULT);
    CHECK_EQ_INT(pt_fault(&p), want);
    CHECK(pt_snapshot(&p).latched);
    CHECK(!pt_data_en(&p));
    CHECK(pt_vbus_en(&p)); /* data first */
    step(&p, 103, pg_after, false, use_force);
    CHECK(!pt_vbus_en(&p));
    CHECK_EQ_INT(pt_state(&p), PT_FAULT);
    CHECK(run_in(&p, 104, 200, false, false, use_force, PT_FAULT));
    CHECK_EQ_INT(pt_fault(&p), want);
}

TEST(test_drop_and_off_same_tick_latches)
{
    drop_and_off_variant(false, false, PT_FAULT_EFUSE);
    drop_and_off_variant(true, false, PT_FAULT_OUTPUT);
    drop_and_off_variant(false, true, PT_FAULT_EFUSE);
    drop_and_off_variant(true, true, PT_FAULT_OUTPUT);
}

TEST(test_tokens)
{
    CHECK_EQ_STR(pt_state_token(PT_OFF), "off");
    CHECK_EQ_STR(pt_state_token(PT_DISCHARGE), "discharge");
    CHECK_EQ_STR(pt_state_token(PT_BLOCKED), "blocked");
    CHECK_EQ_STR(pt_state_token(PT_POWERING), "powering");
    CHECK_EQ_STR(pt_state_token(PT_NO_INPUT), "no_input");
    CHECK_EQ_STR(pt_state_token(PT_SETTLE), "powering");
    CHECK_EQ_STR(pt_state_token(PT_POWER_ONLY), "power");
    CHECK_EQ_STR(pt_state_token(PT_ON), "on");
    CHECK_EQ_STR(pt_state_token(PT_SHUTDOWN), "off");
    CHECK_EQ_STR(pt_state_token(PT_FAULT), "fault");
    CHECK_EQ_STR(pt_fault_token(PT_FAULT_NONE), "none");
    CHECK_EQ_STR(pt_fault_token(PT_FAULT_EFUSE), "efuse");
    CHECK_EQ_STR(pt_fault_token(PT_FAULT_OUTPUT), "output");
    pt_t p;
    pt_init(&p, 0);
    pt_snapshot_t s = pt_snapshot(&p);
    CHECK(s.usb_req && s.data_req && !s.latched);
    CHECK_EQ_INT(s.cause, PT_FAULT_NONE);
}

int main(void)
{
    RUN(test_cold_connect);
    RUN(test_ext_at_reset_never_powers);
    RUN(test_power_only_and_back);
    RUN(test_no_input_retry);
    RUN(test_pg_only_pulse_is_no_input);
    RUN(test_startup_short_retried);
    RUN(test_powering_not_want_power_shuts_down);
    RUN(test_settle_drop_faults);
    RUN(test_post_on_drop_latches_efuse);
    RUN(test_post_on_out_drop_is_output_fault);
    RUN(test_single_glitch_ignored);
    RUN(test_ext_power_discharge_then_connect);
    RUN(test_ext_power_blocked);
    RUN(test_not_want_power_exits_to_off);
    RUN(test_ext_reported_in_fault_and_no_input);
    RUN(test_force_off_button);
    RUN(test_disconnect_for);
    RUN(test_restore_latched_snapshot);
    RUN(test_rearm_respects_en_low_hold);
    RUN(test_pt_timers_across_wrap);
    RUN(test_init_holds_en_low);
    RUN(test_drop_and_off_same_tick_latches);
    RUN(test_tokens);
    return minitest_summary();
}

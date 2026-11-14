/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/seq.h"
#include "minitest.h"

/* Tick convention (global constraints): init at now = 0, ticks at 1, 2, 3, ...; seq_start(now = T0) arms and
 * steps run from seq_tick(T0 + 1). Per tick, in main-loop order: inputs_update, seq_tick, relays_tick,
 * pt_tick, so a USB step sees the passthrough state of the previous tick.
 *
 * Passthrough plant model (unless `forced`): PG_OK = OUT_OK = supply AND VBUS_EN of the previous tick. With
 * `forced`, the raw status pair is (pg, out). Status debounce is 3 samples, so a level first sampled at t is
 * debounced at t + 2. Cold connect with supply from init: POWERING 3, SETTLE 6, ON 16. */

typedef struct {
    relays_t r;
    pt_t pt;
    inputs_t in;
    seq_t s;
    bool inraw[IN_N];
    bool supply, forced, pg, out;
    ms_t now;
    ms_t watch_from;   /* data_seen tracks data_en == 1 at ticks >= watch_from */
    bool watching, data_seen;
} env_t;

static void env_init(env_t *e, bool supply)
{
    memset(e, 0, sizeof *e);
    relays_init(&e->r);
    /* Passthrough initialised PT_EN_LOW_HOLD_MS before tick 0: its init EN-low hold (pinned in
     * test_passthrough) is complete at tick 1, so the timings below are those of a long-low EN. */
    pt_init(&e->pt, (ms_t)0 - PT_EN_LOW_HOLD_MS);
    inputs_init(&e->in, e->inraw, 0);
    seq_init(&e->s);
    e->supply = supply;
}

static void tick(env_t *e, ms_t t)
{
    bool pg, out;
    if (e->forced) {
        pg = e->pg;
        out = e->out;
    } else {
        pg = out = e->supply && pt_vbus_en(&e->pt);
    }
    inputs_update(&e->in, e->inraw, t);
    seq_tick(&e->s, t, &e->r, &e->pt, &e->in);
    relays_tick(&e->r, t, true, 0);
    pt_tick(&e->pt, t, pg, out, false);
    if (e->watching && ms_reached(t, e->watch_from) && pt_data_en(&e->pt)) {
        e->data_seen = true;
    }
    e->now = t;
}

static void run_to(env_t *e, ms_t to)
{
    while (e->now != to) {
        tick(e, e->now + 1);
    }
}

static void start(env_t *e, const seq_prog_t *p)
{
    seq_start(&e->s, p, &e->pt, e->now);
}

static bool token_is(const seq_t *s, const char *want)
{
    char buf[48];
    seq_status_token(s, buf, sizeof buf);
    if (strcmp(buf, want) != 0) {
        printf("  token \"%s\" != \"%s\"\n", buf, want);
        return false;
    }
    return true;
}

/* ---- program builders ---- */
static seq_step_t st_set(uint8_t mask, bool on)
{
    seq_step_t s = {ST_SET, mask, on, 0, COND_HIGH, 0};
    return s;
}
static seq_step_t st_usb(bool on)
{
    seq_step_t s = {ST_USB, 0, on, 0, COND_HIGH, 0};
    return s;
}
static seq_step_t st_data(bool on)
{
    seq_step_t s = {ST_USB_DATA, 0, on, 0, COND_HIGH, 0};
    return s;
}
static seq_step_t st_delay(ms_t ms)
{
    seq_step_t s = {ST_DELAY, 0, false, 0, COND_HIGH, ms};
    return s;
}
static seq_step_t st_wait(uint8_t in0, in_cond_t c, ms_t ms)
{
    seq_step_t s = {ST_WAIT, 0, false, in0, c, ms};
    return s;
}
static void prog_add(seq_prog_t *p, seq_step_t s)
{
    p->step[p->n++] = s;
}

/* ---------------------------------------------------------------- */

TEST(test_esp32_boot_sequence)
{
    env_t e;
    env_init(&e, false);
    seq_prog_t p = {0};
    prog_add(&p, st_set(0x01, true));
    prog_add(&p, st_set(0x02, true));
    prog_add(&p, st_delay(3000));
    prog_add(&p, st_set(0x01, false));
    prog_add(&p, st_delay(100));
    prog_add(&p, st_set(0x02, false));
    CHECK_EQ_INT(seq_validate(&p, e.r.max_on), E_OK);
    start(&e, &p);
    CHECK(seq_running(&e.s));
    CHECK(token_is(&e.s, "running:1/6"));

    bool ok03 = true, ok02 = true;
    for (ms_t t = 1; t <= 3101; t++) {
        tick(&e, t);
        if (t == 1) {
            CHECK(token_is(&e.s, "running:3/6"));
        }
        if (t <= 3000 && relays_req_mask(&e.r) != 0x03) {
            ok03 = false;
        }
        if (t >= 3001 && t <= 3100 && relays_req_mask(&e.r) != 0x02) {
            ok02 = false;
        }
        if (t == 3100) {
            CHECK_EQ_INT(e.s.status, SEQ_RUNNING);
        }
    }
    CHECK(ok03);
    CHECK(ok02);
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x00);
    CHECK_EQ_INT(e.s.status, SEQ_DONE);
    CHECK(!seq_running(&e.s));
    CHECK(token_is(&e.s, "done"));
}

TEST(test_validate_limit)
{
    ms_t mx[RLY_N] = {1000, 1000, 1000, 1000, 1000};
    seq_prog_t p = {0};
    prog_add(&p, st_set(0x01, true));
    prog_add(&p, st_delay(2000));
    prog_add(&p, st_set(0x01, false));
    CHECK_EQ_INT(seq_validate(&p, mx), E_LIMIT);
    mx[0] = 0; /* MAX_ON 0 = unlimited */
    CHECK_EQ_INT(seq_validate(&p, mx), E_OK);
    mx[0] = 1000;

    /* equal to max_on is allowed */
    p.step[1] = st_delay(1000);
    CHECK_EQ_INT(seq_validate(&p, mx), E_OK);

    /* WAIT counts with its full timeout */
    seq_prog_t w = {0};
    prog_add(&w, st_set(0x02, true));
    prog_add(&w, st_wait(0, COND_HIGH, 600));
    prog_add(&w, st_delay(500));
    prog_add(&w, st_set(0x02, false));
    CHECK_EQ_INT(seq_validate(&w, mx), E_LIMIT);

    /* time after the OFF does not count */
    seq_prog_t o = {0};
    prog_add(&o, st_set(0x01, true));
    prog_add(&o, st_delay(900));
    prog_add(&o, st_set(0x01, false));
    prog_add(&o, st_delay(5000));
    CHECK_EQ_INT(seq_validate(&o, mx), E_OK);

    /* relay left ON at the end: counted up to the program end */
    seq_prog_t l = {0};
    prog_add(&l, st_set(0x01, true));
    prog_add(&l, st_delay(1001));
    CHECK_EQ_INT(seq_validate(&l, mx), E_LIMIT);

    /* a repeated ON restarts the count (relays_set restarts on_since) */
    seq_prog_t r = {0};
    prog_add(&r, st_set(0x01, true));
    prog_add(&r, st_delay(900));
    prog_add(&r, st_set(0x01, true));
    prog_add(&r, st_delay(900));
    CHECK_EQ_INT(seq_validate(&r, mx), E_OK);

    /* ALL: only channel 5 limited */
    ms_t m5[RLY_N] = {0, 0, 0, 0, 1000};
    seq_prog_t a = {0};
    prog_add(&a, st_set(0x1F, true));
    prog_add(&a, st_delay(1001));
    prog_add(&a, st_set(0x1F, false));
    CHECK_EQ_INT(seq_validate(&a, m5), E_LIMIT);
    a.step[1] = st_delay(1000);
    CHECK_EQ_INT(seq_validate(&a, m5), E_OK);

    /* empty / oversized program */
    seq_prog_t z = {0};
    CHECK_EQ_INT(seq_validate(&z, mx), E_ARG);
    z.n = SEQ_MAX_STEPS + 1;
    CHECK_EQ_INT(seq_validate(&z, mx), E_ARG);
}

TEST(test_wait_step_timeout_aborts_and_cleans)
{
    env_t e;
    env_init(&e, false); /* inputs low */
    seq_prog_t p = {0};
    prog_add(&p, st_set(0x04, true));
    prog_add(&p, st_wait(0, COND_HIGH, 500));
    start(&e, &p);
    run_to(&e, 500);
    CHECK_EQ_INT(e.s.status, SEQ_RUNNING);
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x04);
    CHECK(token_is(&e.s, "running:2/2"));
    tick(&e, 501);
    CHECK_EQ_INT(e.s.status, SEQ_ABORTED);
    CHECK(token_is(&e.s, "aborted:timeout"));
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x00);
    CHECK_EQ_INT(relays_drive_mask(&e.r), 0x00);
}

TEST(test_wait_step_conditions)
{
    /* HIGH already true: done in the tick it is reached */
    env_t e;
    env_init(&e, false);
    e.inraw[0] = true;
    inputs_init(&e.in, e.inraw, 0);
    seq_prog_t p = {0};
    prog_add(&p, st_wait(0, COND_HIGH, 5));
    start(&e, &p);
    tick(&e, 1);
    CHECK_EQ_INT(e.s.status, SEQ_DONE);

    /* RISE: an input already high does not count; a debounced rise does (raw low 10..19, high 20 -> 22) */
    env_init(&e, false);
    e.inraw[1] = true;
    inputs_init(&e.in, e.inraw, 0);
    seq_prog_t r = {0};
    prog_add(&r, st_wait(1, COND_RISE, 1000));
    start(&e, &r);
    run_to(&e, 9);
    e.inraw[1] = false;
    run_to(&e, 19);
    CHECK_EQ_INT(e.s.status, SEQ_RUNNING); /* the fall does not end a RISE wait */
    e.inraw[1] = true;
    run_to(&e, 21);
    CHECK_EQ_INT(e.s.status, SEQ_RUNNING);
    tick(&e, 22);
    CHECK_EQ_INT(e.s.status, SEQ_DONE);

    /* LOW with a low input, FALL with a debounced fall */
    env_init(&e, false);
    seq_prog_t l = {0};
    prog_add(&l, st_wait(3, COND_LOW, 10));
    start(&e, &l);
    tick(&e, 1);
    CHECK_EQ_INT(e.s.status, SEQ_DONE);

    env_init(&e, false);
    e.inraw[2] = true;
    inputs_init(&e.in, e.inraw, 0);
    seq_prog_t f = {0};
    prog_add(&f, st_wait(2, COND_FALL, 1000));
    start(&e, &f);
    run_to(&e, 4);
    e.inraw[2] = false;
    run_to(&e, 6);
    CHECK_EQ_INT(e.s.status, SEQ_RUNNING);
    tick(&e, 7);
    CHECK_EQ_INT(e.s.status, SEQ_DONE);
}

/* Cold connect from init (plant with supply); ON at 16, run to 50. */
static void env_connected(env_t *e)
{
    env_init(e, true);
    run_to(e, 50);
    CHECK_EQ_INT(pt_state(&e->pt), PT_ON);
    CHECK(pt_data_en(&e->pt));
    CHECK(pt_vbus_en(&e->pt));
}

TEST(test_usb_off_completes_when_physically_off)
{
    env_t e;
    env_connected(&e);
    seq_prog_t p = {0};
    prog_add(&p, st_usb(false));
    prog_add(&p, st_set(0x01, true));
    start(&e, &p); /* 50 */
    tick(&e, 51); /* SHUTDOWN: data 0, VBUS still 1 */
    CHECK(!pt_data_en(&e.pt));
    CHECK(pt_vbus_en(&e.pt));
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x00);
    tick(&e, 52); /* VBUS 0; the step saw VBUS 1 */
    CHECK(!pt_vbus_en(&e.pt));
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x00);
    CHECK_EQ_INT(e.s.status, SEQ_RUNNING);
    tick(&e, 53); /* both EN seen 0 -> SET applied */
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x01);
    CHECK_EQ_INT(e.s.status, SEQ_DONE);
    CHECK(!pt_snapshot(&e.pt).usb_req); /* success keeps the end state */
}

TEST(test_usb_on_fails_on_no_input)
{
    env_t e;
    env_init(&e, false); /* no supply on USB-B */
    pt_set_usb(&e.pt, false);
    run_to(&e, 50);
    CHECK_EQ_INT(pt_state(&e.pt), PT_OFF);
    seq_prog_t p = {0};
    prog_add(&p, st_set(0x10, true));
    prog_add(&p, st_usb(true));
    prog_add(&p, st_delay(10));
    start(&e, &p);
    tick(&e, 51);
    CHECK_EQ_INT(pt_state(&e.pt), PT_POWERING);
    run_to(&e, 151);
    CHECK_EQ_INT(pt_state(&e.pt), PT_NO_INPUT);
    CHECK_EQ_INT(e.s.status, SEQ_RUNNING);
    tick(&e, 152);
    CHECK_EQ_INT(e.s.status, SEQ_ABORTED);
    CHECK(token_is(&e.s, "aborted:usb_no_input"));
    CHECK(!pt_snapshot(&e.pt).usb_req);
    CHECK(pt_snapshot(&e.pt).data_req);
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x00);
}

TEST(test_abort_restores_usb_requests)
{
    env_t e;
    env_connected(&e);
    seq_prog_t p = {0};
    prog_add(&p, st_usb(false));
    prog_add(&p, st_data(false));
    prog_add(&p, st_delay(5000));
    start(&e, &p); /* 50 */
    run_to(&e, 100);
    CHECK(!pt_snapshot(&e.pt).usb_req);
    CHECK(!pt_snapshot(&e.pt).data_req);
    CHECK_EQ_INT(pt_state(&e.pt), PT_OFF);
    seq_abort(&e.s, "abort", &e.r, &e.pt);
    CHECK(token_is(&e.s, "aborted:abort"));
    CHECK(pt_snapshot(&e.pt).usb_req);
    CHECK(pt_snapshot(&e.pt).data_req);
    run_to(&e, 300); /* normal connect */
    CHECK_EQ_INT(pt_state(&e.pt), PT_ON);
}

/* M6 / R2-M-1: pre-SEQ fault latched (cause output); the SEQ's USB ON rearms and connects, the WAIT fails at
 * k = 216: SHUTDOWN at k (data 0), VBUS 0 at k+1, FAULT at k+11 with the pre-SEQ cause; data_en never 1 from k. */
TEST(test_abort_restores_latch)
{
    env_t e;
    env_connected(&e);
    e.forced = true; /* output short: PG 1, OUT 0 -> cause output */
    e.pg = true;
    e.out = false;
    run_to(&e, 60);
    CHECK_EQ_INT(pt_state(&e.pt), PT_FAULT);
    CHECK_EQ_INT(pt_fault(&e.pt), PT_FAULT_OUTPUT);
    CHECK(!pt_vbus_en(&e.pt));
    e.forced = false; /* plant: VBUS 0 -> status 00 */
    run_to(&e, 100);
    CHECK_EQ_INT(pt_state(&e.pt), PT_FAULT);

    seq_prog_t p = {0};
    prog_add(&p, st_usb(true));
    prog_add(&p, st_wait(0, COND_HIGH, 100));
    start(&e, &p); /* 100 */
    run_to(&e, 115);
    CHECK_EQ_INT(pt_state(&e.pt), PT_ON); /* rearmed and connected by the SEQ (allowed) */
    CHECK(pt_data_en(&e.pt));
    CHECK_EQ_INT(pt_fault(&e.pt), PT_FAULT_NONE);
    tick(&e, 116);
    CHECK(token_is(&e.s, "running:2/2"));
    run_to(&e, 215);
    CHECK_EQ_INT(e.s.status, SEQ_RUNNING);

    e.watching = true;
    e.watch_from = 216;
    tick(&e, 216);
    CHECK(token_is(&e.s, "aborted:timeout"));
    CHECK_EQ_INT(pt_state(&e.pt), PT_SHUTDOWN);
    CHECK(!pt_data_en(&e.pt));
    CHECK(pt_vbus_en(&e.pt));
    CHECK(pt_snapshot(&e.pt).latched);
    CHECK_EQ_INT(pt_fault(&e.pt), PT_FAULT_OUTPUT);
    tick(&e, 217);
    CHECK(!pt_vbus_en(&e.pt));
    run_to(&e, 226);
    CHECK_EQ_INT(pt_state(&e.pt), PT_SHUTDOWN);
    tick(&e, 227);
    CHECK_EQ_INT(pt_state(&e.pt), PT_FAULT);
    run_to(&e, 3000);
    CHECK_EQ_INT(pt_state(&e.pt), PT_FAULT);
    CHECK_EQ_INT(pt_fault(&e.pt), PT_FAULT_OUTPUT);
    CHECK(pt_snapshot(&e.pt).usb_req);
    CHECK(!e.data_seen);
}

/* Restoring never bypasses FAULT: a fault latched while the SEQ runs survives the cleanup. */
TEST(test_abort_keeps_new_fault)
{
    env_t e;
    env_connected(&e);
    seq_prog_t p = {0};
    prog_add(&p, st_delay(1000));
    start(&e, &p); /* 50, no fault before */
    run_to(&e, 60);
    e.forced = true;
    e.pg = false;
    e.out = false;
    run_to(&e, 100);
    CHECK_EQ_INT(pt_state(&e.pt), PT_FAULT);
    CHECK_EQ_INT(pt_fault(&e.pt), PT_FAULT_EFUSE);
    seq_abort(&e.s, "abort", &e.r, &e.pt);
    CHECK(pt_snapshot(&e.pt).latched);
    CHECK_EQ_INT(pt_fault(&e.pt), PT_FAULT_EFUSE);
    e.forced = false;
    e.watching = true;
    e.watch_from = 101;
    run_to(&e, 3000);
    CHECK_EQ_INT(pt_state(&e.pt), PT_FAULT);
    CHECK(!e.data_seen);
}

TEST(test_success_keeps_end_state)
{
    env_t e;
    env_init(&e, false);
    seq_prog_t p = {0};
    prog_add(&p, st_set(0x08, true));
    start(&e, &p);
    tick(&e, 1);
    CHECK_EQ_INT(e.s.status, SEQ_DONE);
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x08);
    run_to(&e, 10);
    uint32_t left = 0;
    bool inf = true;
    CHECK(relays_left(&e.r, 3, e.now, &left, &inf));
    CHECK_EQ_INT(left, 59991); /* on_since 1 + 60000 - 10 */
    CHECK(!inf);
    /* a safety event after success changes nothing in the SEQ result */
    seq_abort(&e.s, "bus_reset", &e.r, &e.pt);
    CHECK(token_is(&e.s, "done"));
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x08);
    run_to(&e, 60000);
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x08);
    tick(&e, 60001);
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x00);
}

TEST(test_usb_data_steps)
{
    env_t e;
    env_connected(&e);
    const ms_t b = e.now; /* 50 */
    seq_prog_t p = {0};
    prog_add(&p, st_data(false));
    prog_add(&p, st_delay(10));
    prog_add(&p, st_data(true));
    start(&e, &p);
    tick(&e, b + 1);
    CHECK(!pt_data_en(&e.pt));
    CHECK(pt_vbus_en(&e.pt));
    CHECK(token_is(&e.s, "running:1/3"));
    tick(&e, b + 2);
    CHECK(token_is(&e.s, "running:2/3"));
    CHECK_EQ_INT(e.s.t0, b + 2);
    run_to(&e, b + 11);
    CHECK(!pt_data_en(&e.pt));
    tick(&e, b + 12);
    CHECK(pt_data_en(&e.pt));
    CHECK_EQ_INT(e.s.status, SEQ_RUNNING);
    tick(&e, b + 13);
    CHECK_EQ_INT(e.s.status, SEQ_DONE);
}

TEST(test_usb_on_timeout)
{
    env_t e;
    env_init(&e, false);
    pt_set_usb(&e.pt, false);
    e.forced = true; /* external voltage on the output: DISCHARGE, BLOCKED only after 2000 ms */
    e.pg = false;
    e.out = true;
    seq_prog_t p = {0};
    prog_add(&p, st_usb(true));
    start(&e, &p); /* 0 */
    run_to(&e, 1000);
    CHECK_EQ_INT(pt_state(&e.pt), PT_DISCHARGE);
    CHECK_EQ_INT(e.s.status, SEQ_RUNNING);
    tick(&e, 1001);
    CHECK_EQ_INT(e.s.status, SEQ_ABORTED);
    CHECK(token_is(&e.s, "aborted:usb_timeout"));
    CHECK(!pt_snapshot(&e.pt).usb_req);
    tick(&e, 1002);
    CHECK_EQ_INT(pt_state(&e.pt), PT_OFF);
}

TEST(test_usb_on_blocked)
{
    env_t e;
    env_init(&e, false);
    e.forced = true; /* usb_req 1 from init and external voltage: DISCHARGE 3, BLOCKED 2003 */
    e.pg = false;
    e.out = true;
    run_to(&e, 2010);
    CHECK_EQ_INT(pt_state(&e.pt), PT_BLOCKED);
    seq_prog_t p = {0};
    prog_add(&p, st_usb(true));
    start(&e, &p);
    tick(&e, 2011); /* failure states count from the tick after the step is reached */
    CHECK_EQ_INT(e.s.status, SEQ_RUNNING);
    tick(&e, 2012);
    CHECK(token_is(&e.s, "aborted:usb_blocked"));
}

TEST(test_usb_on_fault)
{
    /* The status drops during SETTLE (after both OK was seen, before ON / POWER_ONLY): FAULT latched while the
     * USB ON step waits -> usb_fault. */
    env_t e;
    env_init(&e, true);
    pt_set_usb(&e.pt, false);
    run_to(&e, 50);
    seq_prog_t p = {0};
    prog_add(&p, st_usb(true));
    start(&e, &p); /* 50: POWERING 51, status 11 debounced 54 -> SETTLE 54 */
    run_to(&e, 55);
    CHECK_EQ_INT(pt_state(&e.pt), PT_SETTLE);
    e.forced = true;
    e.pg = false;
    e.out = false;
    run_to(&e, 58); /* 00 sampled 56, 57, 58 -> FAULT efuse at 58 */
    CHECK_EQ_INT(pt_state(&e.pt), PT_FAULT);
    CHECK_EQ_INT(e.s.status, SEQ_RUNNING);
    tick(&e, 59);
    CHECK(token_is(&e.s, "aborted:usb_fault"));
    /* pre-SEQ: no latch, usb_req 0; the new fault is kept */
    CHECK(!pt_snapshot(&e.pt).usb_req);
    CHECK(pt_snapshot(&e.pt).latched);
    CHECK_EQ_INT(pt_fault(&e.pt), PT_FAULT_EFUSE);
}

TEST(test_safety_reasons)
{
    static const char *const reasons[] = {"abort", "timeout", "usb_no_input", "usb_blocked", "usb_fault",
                                          "usb_timeout", "suspend", "bus_reset", "unconfigured", "reboot",
                                          "bootloader"};
    for (size_t i = 0; i < sizeof reasons / sizeof reasons[0]; i++) {
        env_t e;
        env_init(&e, false);
        relays_set(&e.r, 0x10, true, 0); /* not touched by the SEQ: stays ON */
        seq_prog_t p = {0};
        prog_add(&p, st_set(0x01, true));
        prog_add(&p, st_delay(100));
        start(&e, &p);
        tick(&e, 1);
        CHECK_EQ_INT(relays_req_mask(&e.r), 0x11);
        seq_abort(&e.s, reasons[i], &e.r, &e.pt);
        char want[48];
        snprintf(want, sizeof want, "aborted:%s", reasons[i]);
        CHECK(token_is(&e.s, want));
        CHECK(!seq_running(&e.s));
        CHECK_EQ_INT(relays_req_mask(&e.r), 0x10);
        tick(&e, 2); /* aborted SEQ does nothing more */
        CHECK(token_is(&e.s, want));
        CHECK_EQ_INT(relays_req_mask(&e.r), 0x10);
    }
}

TEST(test_abort_idle_and_tokens)
{
    env_t e;
    env_init(&e, false);
    relays_set(&e.r, 0x01, true, 0);
    CHECK(token_is(&e.s, "idle"));
    seq_abort(&e.s, "abort", &e.r, &e.pt);
    CHECK(token_is(&e.s, "idle"));
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x01);
    CHECK(pt_snapshot(&e.pt).usb_req);

    /* truncation: snprintf semantics */
    char small[4];
    int n = seq_status_token(&e.s, small, sizeof small);
    CHECK_EQ_INT(n, 4);
    CHECK_EQ_STR(small, "idl");

    /* a new SEQ clears the previous result */
    seq_prog_t p = {0};
    prog_add(&p, st_delay(5));
    start(&e, &p);
    seq_abort(&e.s, "abort", &e.r, &e.pt);
    CHECK(token_is(&e.s, "aborted:abort"));
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x01); /* untouched */
    start(&e, &p);
    CHECK(token_is(&e.s, "running:1/1"));
    run_to(&e, 5);
    CHECK(token_is(&e.s, "running:1/1"));
    tick(&e, 6);
    CHECK(token_is(&e.s, "done"));
}

TEST(test_start_only_arms)
{
    /* seq_tick at the start time itself does nothing; steps begin at T0 + 1. */
    env_t e;
    env_init(&e, false);
    run_to(&e, 10);
    seq_prog_t p = {0};
    prog_add(&p, st_set(0x02, true));
    start(&e, &p);
    seq_tick(&e.s, 10, &e.r, &e.pt, &e.in);
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x00);
    tick(&e, 11);
    CHECK_EQ_INT(relays_req_mask(&e.r), 0x02);
    CHECK_EQ_INT(e.s.status, SEQ_DONE);
}

TEST(test_reply_fields)
{
    seq_prog_t p = {0};
    prog_add(&p, st_set(0x01, true));
    prog_add(&p, st_delay(3000));
    prog_add(&p, st_delay(100));
    CHECK_EQ_INT(seq_delay_ms(&p), 3100);
    CHECK(!seq_async(&p));
    prog_add(&p, st_wait(0, COND_HIGH, 1000));
    CHECK_EQ_INT(seq_delay_ms(&p), 3100); /* WAIT not counted */
    CHECK(seq_async(&p));
    seq_prog_t u = {0};
    prog_add(&u, st_usb(false));
    CHECK(seq_async(&u));
    seq_prog_t d = {0};
    prog_add(&d, st_data(true));
    CHECK(seq_async(&d));
}

int main(void)
{
    RUN(test_esp32_boot_sequence);
    RUN(test_validate_limit);
    RUN(test_wait_step_timeout_aborts_and_cleans);
    RUN(test_wait_step_conditions);
    RUN(test_usb_off_completes_when_physically_off);
    RUN(test_usb_on_fails_on_no_input);
    RUN(test_abort_restores_usb_requests);
    RUN(test_abort_restores_latch);
    RUN(test_abort_keeps_new_fault);
    RUN(test_success_keeps_end_state);
    RUN(test_usb_data_steps);
    RUN(test_usb_on_timeout);
    RUN(test_usb_on_blocked);
    RUN(test_usb_on_fault);
    RUN(test_safety_reasons);
    RUN(test_abort_idle_and_tokens);
    RUN(test_start_only_arms);
    RUN(test_reply_fields);
    return minitest_summary();
}

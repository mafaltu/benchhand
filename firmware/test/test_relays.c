/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/relays.h"
#include "minitest.h"

/* Tick model of these unit tests (coordinator ruling M20 for Task 4, an exception to the global convention):
 * the tests call the module API directly, so a request made with now = t is followed by relays_tick(t) in the
 * same ms. In the firmware (global convention, app layer) a command runs with the now of the last tick T and
 * the coils change in relays_tick(T + 1); do not copy this model into integration tests.
 * Ticks run every 1 ms unless a test says otherwise. */

/* Tick every ms from `from` to `to` inclusive (wrap-safe). */
static void run(relays_t *r, ms_t from, ms_t to, bool allowed, uint8_t btn)
{
    for (ms_t t = from;; t++) {
        relays_tick(r, t, allowed, btn);
        if (t == to) {
            break;
        }
    }
}

/* Tick every ms from `from` to `to` inclusive; true if drive_mask equalled `mask` after every tick. */
static bool run_expect(relays_t *r, ms_t from, ms_t to, bool allowed, uint8_t btn, uint8_t mask)
{
    bool ok = true;
    for (ms_t t = from;; t++) {
        relays_tick(r, t, allowed, btn);
        if (relays_drive_mask(r) != mask) {
            ok = false;
        }
        if (t == to) {
            break;
        }
    }
    return ok;
}

TEST(test_init_all_off)
{
    relays_t r;
    relays_init(&r);
    relays_tick(&r, 0, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
    for (uint8_t i = 0; i < RLY_N; i++) {
        CHECK_EQ_INT(r.max_on[i], RLY_MAX_ON_DEFAULT);
    }
}

TEST(test_xor_table)
{
    /* brief 9: req x button -> drive (allowed = true) */
    static const struct { bool req; uint8_t btn; uint8_t drive; } rows[4] = {
        {false, 0, 0}, {true, 0, 1}, {false, 1, 1}, {true, 1, 0},
    };
    for (int i = 0; i < 4; i++) {
        relays_t r;
        relays_init(&r);
        relays_tick(&r, 0, true, 0);
        if (rows[i].req) {
            relays_set(&r, 0x01, true, 1);
        }
        relays_tick(&r, 1, true, rows[i].btn);
        CHECK_EQ_INT(relays_drive_mask(&r), rows[i].drive);
        CHECK_EQ_INT(relays_req_mask(&r), rows[i].req ? 1 : 0);
    }
}

TEST(test_not_allowed_forces_off)
{
    relays_t r;
    relays_init(&r);
    relays_set(&r, 0x01, true, 0);
    relays_tick(&r, 0, false, 0x01);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
    relays_tick(&r, 1, false, 0x00);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
    relays_tick(&r, 2, false, 0x02);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
    /* allowed true again: req ON wins (button released) */
    relays_tick(&r, 3, true, 0x00);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x01);
    /* a drop of allowed turns everything OFF in that same tick */
    relays_tick(&r, 4, false, 0x00);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
}

TEST(test_set_on_arms_max_on_and_expires)
{
    relays_t r;
    relays_init(&r);
    relays_set(&r, 0x01, true, 0);
    CHECK(run_expect(&r, 0, 59999, true, 0, 0x01));
    relays_tick(&r, 60000, true, 0);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
    /* stays OFF; nothing is restored */
    CHECK(run_expect(&r, 60001, 61000, true, 0, 0));
}

TEST(test_set_on_refresh_extends)
{
    relays_t r;
    relays_init(&r);
    relays_set(&r, 0x01, true, 0);
    run(&r, 0, 49999, true, 0);
    relays_set(&r, 0x01, true, 50000);
    CHECK(run_expect(&r, 50000, 109999, true, 0, 0x01));
    relays_tick(&r, 110000, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
}

TEST(test_set_on_after_pulse_becomes_plain_on)
{
    relays_t r;
    relays_init(&r);
    CHECK_EQ_INT(relays_pulse(&r, 0, 200, 0), E_OK);
    run(&r, 0, 99, true, 0);
    relays_set(&r, 0x01, true, 100);          /* refresh clears pulse_dl */
    CHECK(run_expect(&r, 100, 60099, true, 0, 0x01));
    relays_tick(&r, 60100, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
}

TEST(test_pulse_reject_over_limit)
{
    relays_t r;
    relays_init(&r);
    relays_set_max_on(&r, 0x01, 5000, 0);
    CHECK_EQ_INT(relays_pulse(&r, 0, 6000, 0), E_LIMIT);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
    relays_tick(&r, 0, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);

    /* a rejected PULSE leaves an ON request untouched (deadline unchanged) */
    relays_set(&r, 0x01, true, 1);
    CHECK_EQ_INT(relays_pulse(&r, 0, 5001, 2), E_LIMIT);
    uint32_t left = 0;
    bool inf = true;
    CHECK(relays_left(&r, 0, 2, &left, &inf));
    CHECK_EQ_INT(left, 4999);
    CHECK(!inf);

    /* ms == MAX_ON is accepted */
    relays_init(&r);
    relays_set_max_on(&r, 0x01, 5000, 0);
    CHECK_EQ_INT(relays_pulse(&r, 0, 5000, 0), E_OK);
    CHECK_EQ_INT(relays_req_mask(&r), 0x01);

    /* MAX_ON 0: no limit */
    relays_init(&r);
    relays_set_max_on(&r, 0x01, 0, 0);
    CHECK_EQ_INT(relays_pulse(&r, 0, 3600000u, 0), E_OK);
    CHECK_EQ_INT(relays_req_mask(&r), 0x01);
    CHECK(relays_left(&r, 0, 0, &left, &inf));
    CHECK_EQ_INT(left, 3600000u);
    CHECK(!inf);

    /* bad channel */
    CHECK_EQ_INT(relays_pulse(&r, RLY_N, 10, 0), E_CHANNEL);
}

TEST(test_pulse_ends)
{
    relays_t r;
    relays_init(&r);
    CHECK_EQ_INT(relays_pulse(&r, 0, 200, 0), E_OK);
    CHECK(run_expect(&r, 0, 199, true, 0, 0x01));
    relays_tick(&r, 200, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
}

TEST(test_pulse_capped_by_lowered_max_on)
{
    /* PULSE 5000 at 0, then MAX_ON 1000 at 500: max_dl = on_since + 1000 is the earlier deadline */
    relays_t r;
    relays_init(&r);
    CHECK_EQ_INT(relays_pulse(&r, 0, 5000, 0), E_OK);
    run(&r, 0, 499, true, 0);
    relays_set_max_on(&r, 0x01, 1000, 500);
    CHECK(run_expect(&r, 500, 999, true, 0, 0x01));
    relays_tick(&r, 1000, true, 0);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
}

TEST(test_lower_max_on_live)
{
    relays_t r;
    relays_init(&r);
    relays_set(&r, 0x01, true, 0);
    CHECK(run_expect(&r, 0, 19999, true, 0, 0x01));
    relays_set_max_on(&r, 0x01, 10000, 20000);
    relays_tick(&r, 20000, true, 0);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
}

TEST(test_raise_max_on_live)
{
    relays_t r;
    relays_init(&r);
    relays_set(&r, 0x01, true, 0);
    run(&r, 0, 9999, true, 0);
    relays_set_max_on(&r, 0x01, 100000, 10000);
    CHECK(run_expect(&r, 10000, 99999, true, 0, 0x01));
    relays_tick(&r, 100000, true, 0);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
}

TEST(test_max_on_zero_live_removes_deadline)
{
    relays_t r;
    relays_init(&r);
    relays_set(&r, 0x01, true, 0);
    relays_set_max_on(&r, 0x01, 0, 10);
    relays_tick(&r, 10, true, 0);
    relays_tick(&r, 200000, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x01);
    uint32_t left = 1;
    bool inf = false;
    CHECK(relays_left(&r, 0, 200000, &left, &inf));
    CHECK(inf);
}

TEST(test_max_on_on_off_relay_stored)
{
    relays_t r;
    relays_init(&r);
    relays_set_max_on(&r, 0x01, 5000, 0);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
    CHECK(run_expect(&r, 0, 999, true, 0, 0));
    relays_set(&r, 0x01, true, 1000);
    CHECK(run_expect(&r, 1000, 5999, true, 0, 0x01));
    relays_tick(&r, 6000, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
}

TEST(test_expiry_while_button_held)
{
    relays_t r;
    relays_init(&r);
    relays_set_max_on(&r, 0x01, 1000, 0);
    relays_set(&r, 0x01, true, 0);
    CHECK(run_expect(&r, 0, 499, true, 0, 0x01));
    CHECK(run_expect(&r, 500, 999, true, 0x01, 0));
    relays_tick(&r, 1000, true, 0x01);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x01);
    CHECK(run_expect(&r, 1001, 5000, true, 0x01, 0x01));
    relays_tick(&r, 5001, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
}

TEST(test_max_on_counts_from_request_not_coil)
{
    relays_t r;
    relays_init(&r);
    relays_set_max_on(&r, 0x10, 45, 0);
    relays_set(&r, 0x1F, true, 0);
    run(&r, 0, 39, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r) & 0x10, 0);
    relays_tick(&r, 40, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x1F);
    relays_tick(&r, 41, true, 0);
    uint32_t left = 0;
    bool inf = true;
    CHECK(relays_left(&r, 4, 41, &left, &inf));
    CHECK_EQ_INT(left, 4);
    CHECK(!inf);
    CHECK(run_expect(&r, 42, 44, true, 0, 0x1F));
    relays_tick(&r, 45, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x0F);
    CHECK_EQ_INT(relays_req_mask(&r), 0x0F);
}

TEST(test_max_on_zero_unlimited)
{
    relays_t r;
    relays_init(&r);
    relays_set_max_on(&r, 0x01, 0, 0);
    relays_set(&r, 0x01, true, 0);
    relays_tick(&r, 0, true, 0);
    uint32_t left = 7;
    bool inf = false;
    CHECK(relays_left(&r, 0, 0, &left, &inf));
    CHECK(inf);
    CHECK_EQ_INT(left, 0);
    relays_tick(&r, 5000000u, true, 0);
    relays_tick(&r, 10000000u, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x01);
    CHECK_EQ_INT(relays_req_mask(&r), 0x01);
}

TEST(test_left_values)
{
    relays_t r;
    relays_init(&r);
    uint32_t left = 9;
    bool inf = true;
    CHECK(!relays_left(&r, 0, 0, &left, &inf));
    CHECK_EQ_INT(left, 0);
    CHECK(!inf);
    relays_set(&r, 0x02, true, 100);
    CHECK(relays_left(&r, 1, 100, &left, &inf));
    CHECK_EQ_INT(left, 60000);
    CHECK(!inf);
    CHECK(relays_left(&r, 1, 41350, &left, &inf));
    CHECK_EQ_INT(left, 18750);
    /* PULSE: the earlier deadline counts */
    CHECK_EQ_INT(relays_pulse(&r, 2, 300, 100), E_OK);
    CHECK(relays_left(&r, 2, 150, &left, &inf));
    CHECK_EQ_INT(left, 250);
    /* bad channel */
    CHECK(!relays_left(&r, RLY_N, 0, &left, &inf));
    CHECK_EQ_INT(left, 0);
}

TEST(test_stagger)
{
    relays_t r;
    relays_init(&r);
    relays_set(&r, 0x1F, true, 0);
    relays_tick(&r, 0, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x01);
    CHECK(run_expect(&r, 1, 9, true, 0, 0x01));
    relays_tick(&r, 10, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x03);
    CHECK(run_expect(&r, 11, 19, true, 0, 0x03));
    relays_tick(&r, 20, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x07);
    CHECK(run_expect(&r, 21, 29, true, 0, 0x07));
    relays_tick(&r, 30, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x0F);
    CHECK(run_expect(&r, 31, 39, true, 0, 0x0F));
    relays_tick(&r, 40, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x1F);

    /* second run: a waiting coil whose demand disappears never turns on */
    relays_init(&r);
    relays_set(&r, 0x1F, true, 0);
    run(&r, 0, 14, true, 0);
    relays_set(&r, 0x10, false, 15);
    run(&r, 15, 29, true, 0);
    CHECK(run_expect(&r, 30, 200, true, 0, 0x0F));
}

TEST(test_stagger_applies_to_buttons_and_allowed_edge)
{
    relays_t r;
    relays_init(&r);
    /* two buttons pressed in the same tick: lower channel first, second 10 ms later */
    relays_tick(&r, 0, true, 0x06);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x02);
    CHECK(run_expect(&r, 1, 9, true, 0x06, 0x02));
    relays_tick(&r, 10, true, 0x06);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x06);

    /* allowed drops and returns: coils come back one by one */
    relays_init(&r);
    relays_set(&r, 0x03, true, 0);
    run(&r, 0, 100, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x03);
    relays_tick(&r, 101, false, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
    relays_tick(&r, 102, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x01);
    CHECK(run_expect(&r, 103, 111, true, 0, 0x01));
    relays_tick(&r, 112, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x03);
}

TEST(test_off_never_delayed)
{
    relays_t r;
    relays_init(&r);
    relays_set(&r, 0x1F, true, 0);
    run(&r, 0, 49, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x1F);
    relays_set(&r, 0x1F, false, 50);
    relays_tick(&r, 50, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
    CHECK_EQ_INT(relays_req_mask(&r), 0);

    /* OFF in the tick right after another coil turned ON is not delayed either */
    relays_init(&r);
    relays_set(&r, 0x03, true, 0);
    run(&r, 0, 10, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x03);
    relays_set(&r, 0x01, false, 11);
    relays_tick(&r, 11, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x02);
}

TEST(test_button_not_limited_by_max_on)
{
    relays_t r;
    relays_init(&r);
    CHECK(run_expect(&r, 0, 120000, true, 0x01, 0x01));
    CHECK_EQ_INT(relays_req_mask(&r), 0);
    relays_tick(&r, 120001, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
}

TEST(test_relays_deadline_across_wrap)
{
    relays_t r;
    relays_init(&r);
    const ms_t t0 = 0xFFFFFF00u;
    relays_set_max_on(&r, 0x01, 1000, t0);
    relays_set(&r, 0x01, true, t0);
    uint32_t left = 0;
    bool inf = true;
    CHECK(relays_left(&r, 0, t0, &left, &inf));
    CHECK_EQ_INT(left, 1000);
    CHECK(run_expect(&r, t0, 0x000002E7u, true, 0, 0x01));
    relays_tick(&r, 0x000002E8u, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
}

TEST(test_stagger_across_wrap)
{
    relays_t r;
    relays_init(&r);
    relays_set(&r, 0x03, true, 0xFFFFFFFCu);
    relays_tick(&r, 0xFFFFFFFCu, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x01);
    CHECK(run_expect(&r, 0xFFFFFFFDu, 5, true, 0, 0x01));
    relays_tick(&r, 6, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x03);
}

TEST(test_release_all)
{
    relays_t r;
    relays_init(&r);
    CHECK_EQ_INT(relays_pulse(&r, 0, 500, 100), E_OK);
    relays_set(&r, 0x02, true, 100);
    run(&r, 100, 149, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x03);
    relays_release_all(&r);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
    uint32_t left = 9;
    bool inf = true;
    CHECK(!relays_left(&r, 0, 150, &left, &inf));
    CHECK_EQ_INT(left, 0);
    CHECK(!relays_left(&r, 1, 150, &left, &inf));
    CHECK_EQ_INT(left, 0);
    relays_tick(&r, 150, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
    /* nothing is restored, the old pulse deadline does not matter any more */
    CHECK(run_expect(&r, 151, 1000, true, 0, 0));
    /* MAX_ON is kept (RAM setting, reset only by relays_init) */
    CHECK_EQ_INT(r.max_on[0], RLY_MAX_ON_DEFAULT);
}

TEST(test_mask_bits_above_rly_n_ignored)
{
    relays_t r;
    relays_init(&r);
    relays_set(&r, 0xE0, true, 0);
    relays_set_max_on(&r, 0xE0, 5, 0);
    relays_tick(&r, 0, true, 0xE0);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
    for (uint8_t i = 0; i < RLY_N; i++) {
        CHECK_EQ_INT(r.max_on[i], RLY_MAX_ON_DEFAULT);
    }
}

TEST(test_max_on_set_after_very_long_on)
{
    /* ON for > 2^31 ms with MAX_ON 0, then MAX_ON 1000: on_since + 1000 is long past and must not be
     * mistaken for a future deadline by the wrap-safe compare. */
    relays_t r;
    relays_init(&r);
    relays_set_max_on(&r, 0x01, 0, 0);
    relays_set(&r, 0x01, true, 0);
    relays_tick(&r, 0, true, 0);
    relays_tick(&r, 0x90000000u, true, 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0x01);
    relays_set_max_on(&r, 0x01, 1000, 0x90000001u);
    relays_tick(&r, 0x90000001u, true, 0);
    CHECK_EQ_INT(relays_req_mask(&r), 0);
    CHECK_EQ_INT(relays_drive_mask(&r), 0);
}

int main(void)
{
    RUN(test_init_all_off);
    RUN(test_xor_table);
    RUN(test_not_allowed_forces_off);
    RUN(test_set_on_arms_max_on_and_expires);
    RUN(test_set_on_refresh_extends);
    RUN(test_set_on_after_pulse_becomes_plain_on);
    RUN(test_pulse_reject_over_limit);
    RUN(test_pulse_ends);
    RUN(test_pulse_capped_by_lowered_max_on);
    RUN(test_lower_max_on_live);
    RUN(test_raise_max_on_live);
    RUN(test_max_on_zero_live_removes_deadline);
    RUN(test_max_on_on_off_relay_stored);
    RUN(test_expiry_while_button_held);
    RUN(test_max_on_counts_from_request_not_coil);
    RUN(test_max_on_zero_unlimited);
    RUN(test_left_values);
    RUN(test_stagger);
    RUN(test_stagger_applies_to_buttons_and_allowed_edge);
    RUN(test_off_never_delayed);
    RUN(test_button_not_limited_by_max_on);
    RUN(test_relays_deadline_across_wrap);
    RUN(test_stagger_across_wrap);
    RUN(test_release_all);
    RUN(test_mask_bits_above_rly_n_ignored);
    RUN(test_max_on_set_after_very_long_on);
    return minitest_summary();
}

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/buttons.h"
#include "core/inputs.h"
#include "minitest.h"

/* Tick convention: init at 0, ticks at 1, 2, 3... A raw level set for tick t is the sample at t. */

static void btn_run(buttons_t *b, bool raw[BTN_N], ms_t from, ms_t to)
{
    for (ms_t t = from; t <= to; t++) {
        buttons_update(b, raw, t);
    }
}

TEST(test_debounce_needs_stable_hold)
{
    debounce_t d;
    debounce_init(&d, false, 0);
    bool changed = false;
    for (ms_t t = 1; t <= 30; t++) {
        changed |= debounce_update(&d, ((t / 3) & 1u) != 0, t, 10);
    }
    CHECK(!changed);
    CHECK(!d.stable);
    debounce_init(&d, false, 0);
    for (ms_t t = 1; t <= 399; t++) {
        debounce_update(&d, false, t, 10);
    }
    for (ms_t t = 400; t <= 409; t++) {
        CHECK(!debounce_update(&d, true, t, 10));
        CHECK(!d.stable);
    }
    CHECK(debounce_update(&d, true, 410, 10));
    CHECK(d.stable);
    CHECK(!debounce_update(&d, true, 411, 10));
}

TEST(test_startup_held_blocked_until_release)
{
    buttons_t b;
    bool raw[BTN_N] = {true, false, false, false, false, false};
    buttons_init(&b, raw, 0);
    btn_run(&b, raw, 1, 99);
    CHECK_EQ_INT(buttons_relay_held_mask(&b), 0);
    CHECK_EQ_INT(buttons_raw_held_mask(&b), 1);
    raw[0] = false;
    btn_run(&b, raw, 100, 199);
    CHECK_EQ_INT(buttons_raw_held_mask(&b), 0);
    raw[0] = true;
    btn_run(&b, raw, 200, 209);
    CHECK_EQ_INT(buttons_relay_held_mask(&b), 0);
    btn_run(&b, raw, 210, 210);
    CHECK_EQ_INT(buttons_relay_held_mask(&b), 1);
}

TEST(test_usb_button_never_blocked)
{
    buttons_t b;
    bool raw[BTN_N] = {false, false, false, false, false, true};
    buttons_init(&b, raw, 0);
    CHECK(buttons_usb_held(&b));
    btn_run(&b, raw, 1, 50);
    buttons_rearm(&b);
    CHECK(buttons_usb_held(&b));
    CHECK_EQ_INT(buttons_relay_held_mask(&b), 0);
    raw[5] = false;
    btn_run(&b, raw, 100, 109);
    CHECK(buttons_usb_held(&b));
    btn_run(&b, raw, 110, 110);
    CHECK(!buttons_usb_held(&b));
}

TEST(test_rearm_blocks_currently_held)
{
    buttons_t b;
    bool raw[BTN_N] = {false};
    buttons_init(&b, raw, 0);
    raw[1] = true;
    btn_run(&b, raw, 1, 50);
    CHECK_EQ_INT(buttons_relay_held_mask(&b), 2);
    raw[2] = true; /* relay 3 pressed from 95 */
    btn_run(&b, raw, 95, 100);
    buttons_rearm(&b);
    CHECK_EQ_INT(buttons_relay_held_mask(&b), 0);
    btn_run(&b, raw, 101, 105);
    CHECK_EQ_INT(buttons_relay_held_mask(&b), 0);
    CHECK_EQ_INT(buttons_raw_held_mask(&b), 6);
    btn_run(&b, raw, 106, 300);
    CHECK_EQ_INT(buttons_relay_held_mask(&b), 0);
    CHECK_EQ_INT(b.presses[2], 1); /* blocked press still counted */
    /* release then fresh press works */
    raw[1] = false;
    raw[2] = false;
    btn_run(&b, raw, 301, 320);
    raw[2] = true;
    btn_run(&b, raw, 321, 340);
    CHECK_EQ_INT(buttons_relay_held_mask(&b), 4);
}

TEST(test_press_counter_counts_each_press)
{
    buttons_t b;
    bool raw[BTN_N] = {false};
    buttons_init(&b, raw, 0);
    ms_t t = 1;
    for (int k = 0; k < 3; k++) {
        raw[3] = true;
        btn_run(&b, raw, t, t + 19);
        t += 20;
        raw[3] = false;
        btn_run(&b, raw, t, t + 19);
        t += 20;
    }
    CHECK_EQ_INT(b.presses[3], 3);
    CHECK_EQ_INT(b.presses[0], 0);
}

TEST(test_input_edges_counted)
{
    inputs_t in;
    bool h[IN_N] = {false};
    inputs_init(&in, h, 0);
    for (ms_t t = 1; t < 200; t++) {
        inputs_update(&in, h, t);
    }
    h[1] = true;
    inputs_update(&in, h, 200);
    inputs_update(&in, h, 201);
    CHECK(!inputs_level(&in, 1));
    inputs_update(&in, h, 202);
    CHECK(inputs_level(&in, 1));
    CHECK_EQ_INT(in.rise[1], 1);
    h[1] = false;
    for (ms_t t = 300; t <= 301; t++) {
        inputs_update(&in, h, t);
    }
    CHECK(inputs_level(&in, 1));
    inputs_update(&in, h, 302);
    CHECK(!inputs_level(&in, 1));
    CHECK_EQ_INT(in.fall[1], 1);
    /* 1 ms glitch */
    h[1] = true;
    inputs_update(&in, h, 400);
    h[1] = false;
    for (ms_t t = 401; t <= 410; t++) {
        inputs_update(&in, h, t);
    }
    CHECK_EQ_INT(in.rise[1], 1);
    CHECK_EQ_INT(in.fall[1], 1);
    CHECK(!inputs_level(&in, 1));
}

TEST(test_inputs_cond_met)
{
    /* IN_DEBOUNCE_MS 2: a raw level first sampled at t is stable at t + 2. */
    bool raw[IN_N] = {false, true, false, false};
    inputs_t in;
    inputs_init(&in, raw, 0);
    CHECK(!inputs_cond_met(&in, 0, COND_HIGH, 0, 0));
    CHECK(inputs_cond_met(&in, 0, COND_LOW, 0, 0));
    CHECK(inputs_cond_met(&in, 1, COND_HIGH, 0, 0));
    CHECK(!inputs_cond_met(&in, 1, COND_LOW, 0, 0));
    /* a level that is already there is no edge */
    CHECK(!inputs_cond_met(&in, 1, COND_RISE, in.rise[1], in.fall[1]));
    CHECK(!inputs_cond_met(&in, 0, COND_FALL, in.rise[0], in.fall[0]));

    uint32_t r0 = in.rise[0], f1 = in.fall[1];
    raw[0] = true;  /* rise on input 1 */
    raw[1] = false; /* fall on input 2 */
    inputs_update(&in, raw, 1);
    CHECK(!inputs_cond_met(&in, 0, COND_RISE, r0, 0));
    CHECK(!inputs_cond_met(&in, 1, COND_FALL, 0, f1));
    inputs_update(&in, raw, 2);
    inputs_update(&in, raw, 3);
    CHECK(inputs_cond_met(&in, 0, COND_RISE, r0, 0));
    CHECK(inputs_cond_met(&in, 0, COND_HIGH, 0, 0));
    CHECK(!inputs_cond_met(&in, 0, COND_FALL, 0, in.fall[0]));
    CHECK(inputs_cond_met(&in, 1, COND_FALL, 0, f1));
    CHECK(inputs_cond_met(&in, 1, COND_LOW, 0, 0));
    CHECK(!inputs_cond_met(&in, 1, COND_RISE, in.rise[1], 0));

    /* out of range channel */
    CHECK(!inputs_cond_met(&in, IN_N, COND_LOW, 0, 0));
}

int main(void)
{
    RUN(test_debounce_needs_stable_hold);
    RUN(test_startup_held_blocked_until_release);
    RUN(test_usb_button_never_blocked);
    RUN(test_rearm_blocks_currently_held);
    RUN(test_press_counter_counts_each_press);
    RUN(test_input_edges_counted);
    RUN(test_inputs_cond_met);
    return minitest_summary();
}

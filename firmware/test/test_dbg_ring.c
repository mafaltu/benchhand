/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
/* Host test of the FW-32 debug log queue (src/target/dbg_ring.c) with a fake USART port and a fake clock. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "minitest.h"
#include "target/dbg_ring.h"

#define OUT_MAX 8192

typedef struct {
    bool ready;      /* tx_ready() result while budget allows */
    int budget;      /* bytes tx_ready() still accepts; < 0 = unlimited */
    bool idle;       /* tx_idle() result */
    uint32_t now;    /* fake clock */
    uint32_t step;   /* added to now after every now_ms() call */
    char out[OUT_MAX + 1];
    size_t n;
} fake_t;

static bool f_ready(void *ctx)
{
    fake_t *f = ctx;
    return f->ready && f->budget != 0;
}

static void f_write(void *ctx, uint8_t b)
{
    fake_t *f = ctx;
    if (f->n < OUT_MAX) {
        f->out[f->n++] = (char)b;
        f->out[f->n] = '\0';
    }
    if (f->budget > 0) {
        f->budget--;
    }
}

static bool f_idle(void *ctx)
{
    const fake_t *f = ctx;
    return f->idle;
}

static uint32_t f_now(void *ctx)
{
    fake_t *f = ctx;
    const uint32_t t = f->now;
    f->now += f->step;
    return t;
}

static fake_t fk;
static dbg_port_t port;
static dbg_ring_t rb;

static void setup(void)
{
    memset(&fk, 0, sizeof fk);
    fk.ready = true;
    fk.budget = -1;
    fk.idle = true;
    port.tx_ready = f_ready;
    port.tx_write = f_write;
    port.tx_idle = f_idle;
    port.now_ms = f_now;
    port.ctx = &fk;
    dbg_ring_init(&rb, &port);
}

static void drain(void)
{
    fk.ready = true;
    fk.budget = -1;
    dbg_ring_poll(&rb);
}

/* Queues one line of exactly total bytes with now = 0: "[0] " (4) + text + "\r\n" (2). */
static void emit_len(unsigned total)
{
    char text[DBG_LINE_MAX];
    const unsigned t = total - 6u;
    memset(text, 'a', t);
    text[t] = '\0';
    dbg_ring_printf(&rb, "%s", text);
}

/* Fills the ring to 506 bytes (127 + 127 + 127 + 125) with nothing sent. */
static void fill_506(void)
{
    fk.ready = false;
    emit_len(127u);
    emit_len(127u);
    emit_len(127u);
    emit_len(125u);
    CHECK_EQ_INT(dbg_ring_used(&rb), 506);
    CHECK_EQ_INT(dbg_ring_dropped(&rb), 0);
}

static const char *tail(size_t len)
{
    return fk.n >= len ? &fk.out[fk.n - len] : fk.out;
}

TEST(test_line_format_prefix_and_crlf)
{
    setup();
    fk.now = 1234u;
    dbg_ring_printf(&rb, "hello %d", 5);
    CHECK_EQ_INT(dbg_ring_used(&rb), 16);
    drain();
    CHECK_EQ_STR(fk.out, "[1234] hello 5\r\n");
    CHECK_EQ_INT(dbg_ring_used(&rb), 0);
}

TEST(test_exact_fit_512)
{
    setup();
    fill_506();
    emit_len(6u); /* "[0] \r\n": exactly the 6 free bytes */
    CHECK_EQ_INT(dbg_ring_used(&rb), 512);
    CHECK_EQ_INT(dbg_ring_dropped(&rb), 0);
    drain();
    CHECK_EQ_INT(fk.n, 512);
    CHECK_EQ_STR(tail(12), "aaaa\r\n[0] \r\n");
    CHECK_EQ_INT(dbg_ring_dropped(&rb), 0);
}

TEST(test_one_byte_short_drops_whole_line)
{
    setup();
    fill_506();
    emit_len(7u); /* needs 7, 6 free */
    CHECK_EQ_INT(dbg_ring_used(&rb), 506);
    CHECK_EQ_INT(dbg_ring_dropped(&rb), 1);
    drain();
    CHECK_EQ_INT(fk.n, 506); /* nothing of the dropped line was queued */
    CHECK_EQ_STR(tail(3), "a\r\n");
}

TEST(test_drops_accumulate_notice_once_space_returns)
{
    setup();
    fill_506();
    emit_len(7u);
    dbg_ring_printf(&rb, "b");
    dbg_ring_printf(&rb, "c");
    CHECK_EQ_INT(dbg_ring_dropped(&rb), 3);
    CHECK_EQ_INT(dbg_ring_used(&rb), 506);
    drain(); /* empties the ring; the notice is queued at the end of this poll */
    CHECK_EQ_INT(fk.n, 506);
    CHECK_EQ_INT(dbg_ring_dropped(&rb), 0);
    drain();
    CHECK_EQ_STR(tail(22), "\n[0] [dbg] dropped 3\r\n");
    dbg_ring_printf(&rb, "after");
    drain();
    CHECK_EQ_STR(tail(33), "\n[0] [dbg] dropped 3\r\n[0] after\r\n");
    CHECK_EQ_INT(strstr(fk.out, "b\r\n") == NULL, 1);
    /* a second drain does not repeat the notice */
    drain();
    CHECK_EQ_INT(fk.n, 506 + 21 + 11);
}

TEST(test_pending_notice_keeps_order)
{
    setup();
    fill_506();
    emit_len(7u); /* dropped = 1, 6 free */
    fk.ready = true;
    fk.budget = 10;
    dbg_ring_poll(&rb); /* 16 free: < DBG_NOTICE_MAX, and the 21-byte notice does not fit */
    CHECK_EQ_INT(fk.n, 10);
    CHECK_EQ_INT(dbg_ring_used(&rb), 496);
    CHECK_EQ_INT(dbg_ring_dropped(&rb), 1);
    dbg_ring_printf(&rb, "x"); /* 7 bytes would fit, but must not overtake the notice */
    CHECK_EQ_INT(dbg_ring_used(&rb), 496);
    CHECK_EQ_INT(dbg_ring_dropped(&rb), 2);
    fk.budget = 40;
    dbg_ring_poll(&rb); /* 56 free >= 48: notice queued by poll */
    CHECK_EQ_INT(dbg_ring_dropped(&rb), 0);
    dbg_ring_printf(&rb, "y");
    drain();
    CHECK_EQ_STR(tail(29), "\n[0] [dbg] dropped 2\r\n[0] y\r\n");
    CHECK_EQ_INT(strstr(fk.out, "[0] x") == NULL, 1);
}

TEST(test_notice_queued_by_next_printf)
{
    setup();
    fill_506();
    emit_len(7u);
    fk.ready = true;
    fk.budget = 30; /* 36 free: poll does not queue the notice (< 48) */
    dbg_ring_poll(&rb);
    CHECK_EQ_INT(dbg_ring_dropped(&rb), 1);
    dbg_ring_printf(&rb, "z"); /* notice 21 + line 7 = 28 <= 36 */
    CHECK_EQ_INT(dbg_ring_dropped(&rb), 0);
    drain();
    CHECK_EQ_STR(tail(28), "[0] [dbg] dropped 1\r\n[0] z\r\n");
}

TEST(test_truncation_keeps_prefix_and_crlf)
{
    setup();
    fk.now = 99u;
    char big[301];
    memset(big, 'q', 300);
    big[300] = '\0';
    dbg_ring_printf(&rb, "%s", big);
    CHECK_EQ_INT(dbg_ring_used(&rb), DBG_LINE_MAX - 1);
    drain();
    CHECK_EQ_INT(fk.n, DBG_LINE_MAX - 1);
    CHECK_EQ_INT(strncmp(fk.out, "[99] qqq", 8), 0);
    CHECK_EQ_STR(tail(3), "q\r\n");
    CHECK_EQ_INT(strchr(fk.out, '\0') == &fk.out[fk.n], 1); /* no NUL byte inside the line */
}

TEST(test_io_prints_only_on_change)
{
    setup();
    app_in_t in;
    app_out_t out;
    memset(&in, 0, sizeof in);
    memset(&out, 0, sizeof out);
    dbg_ring_io(&rb, &in, &out); /* first call always prints */
    drain();
    CHECK_EQ_STR(fk.out, "[0] io btn=00 in=0 pg=0 ok=0 | rly=00 vbus=0 data=0 led=0\r\n");
    const size_t n1 = fk.n;
    dbg_ring_io(&rb, &in, &out);
    dbg_ring_io(&rb, &in, &out);
    drain();
    CHECK_EQ_INT(fk.n, n1);
    in.btn_pressed[2] = true;
    in.btn_pressed[BTN_USB] = true;
    in.in_high[3] = true;
    in.pg_ok = true;
    dbg_ring_io(&rb, &in, &out);
    dbg_ring_io(&rb, &in, &out);
    drain();
    CHECK_EQ_STR(&fk.out[n1], "[0] io btn=24 in=8 pg=1 ok=0 | rly=00 vbus=0 data=0 led=0\r\n");
    const size_t n2 = fk.n;
    out.relay_mask = 0x11u;
    out.led_usb = true;
    dbg_ring_io(&rb, &in, &out);
    drain();
    CHECK_EQ_STR(&fk.out[n2], "[0] io btn=24 in=8 pg=1 ok=0 | rly=11 vbus=0 data=0 led=1\r\n");
    const size_t n3 = fk.n;
    out.vbus_en = true; /* each output field alone triggers a line */
    dbg_ring_io(&rb, &in, &out);
    out.data_en = true;
    dbg_ring_io(&rb, &in, &out);
    in.out_ok = true;
    dbg_ring_io(&rb, &in, &out);
    drain();
    CHECK_EQ_STR(&fk.out[n3], "[0] io btn=24 in=8 pg=1 ok=0 | rly=11 vbus=1 data=0 led=1\r\n"
                              "[0] io btn=24 in=8 pg=1 ok=0 | rly=11 vbus=1 data=1 led=1\r\n"
                              "[0] io btn=24 in=8 pg=1 ok=1 | rly=11 vbus=1 data=1 led=1\r\n");
}

TEST(test_flush_completes)
{
    setup();
    dbg_ring_printf(&rb, "one");
    dbg_ring_printf(&rb, "two");
    fk.ready = true;
    fk.step = 1u;
    CHECK_EQ_INT(dbg_ring_flush(&rb, 60u), 1);
    CHECK_EQ_STR(fk.out, "[0] one\r\n[0] two\r\n");
    CHECK_EQ_INT(dbg_ring_used(&rb), 0);
}

TEST(test_flush_times_out_at_bound_tx_stuck)
{
    setup();
    dbg_ring_printf(&rb, "stuck");
    fk.ready = false; /* TXE never sets */
    fk.now = 1000u;
    fk.step = 1u;
    CHECK_EQ_INT(dbg_ring_flush(&rb, 60u), 0);
    CHECK_EQ_INT(fk.n, 0);
    CHECK_EQ_INT(dbg_ring_used(&rb), 11); /* "[0] stuck\r\n" */
    /* t0 read at 1000; returns at the first read with elapsed >= 60, i.e. the read that returned 1060 */
    CHECK_EQ_INT(fk.now, 1061);
}

TEST(test_flush_waits_for_tc)
{
    setup();
    dbg_ring_printf(&rb, "w");
    fk.ready = true;
    fk.idle = false; /* ring drains, last byte never completes */
    fk.step = 1u;
    CHECK_EQ_INT(dbg_ring_flush(&rb, 60u), 0);
    CHECK_EQ_STR(fk.out, "[0] w\r\n");
    CHECK_EQ_INT(fk.now, 61); /* t0 read returned 0, the read that returned 60 ends it */
}

int main(void)
{
    RUN(test_line_format_prefix_and_crlf);
    RUN(test_exact_fit_512);
    RUN(test_one_byte_short_drops_whole_line);
    RUN(test_drops_accumulate_notice_once_space_returns);
    RUN(test_pending_notice_keeps_order);
    RUN(test_notice_queued_by_next_printf);
    RUN(test_truncation_keeps_prefix_and_crlf);
    RUN(test_io_prints_only_on_change);
    RUN(test_flush_completes);
    RUN(test_flush_times_out_at_bound_tx_stuck);
    RUN(test_flush_waits_for_tc);
    return minitest_summary();
}

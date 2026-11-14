/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include <stdlib.h>

#include "core/app.h"
#include "minitest.h"

/* ---------- fixture: fake env with a growable TX buffer and settable tx_space ---------- */

#define TXCAP 200000u

typedef struct {
    char buf[TXCAP];
    size_t len;
    size_t space; /* what tx_space reports (clamped to the free room) */
} fake_tx_t;

static size_t fx_space(void *ctx)
{
    fake_tx_t *t = ctx;
    size_t room = TXCAP - 1u - t->len;
    return t->space < room ? t->space : room;
}

static void fx_write(void *ctx, const char *s, size_t n)
{
    fake_tx_t *t = ctx;
    CHECK(n <= fx_space(ctx));
    memcpy(t->buf + t->len, s, n);
    t->len += n;
    t->buf[t->len] = '\0';
}

typedef struct {
    app_t a;
    app_env_t env;
    fake_tx_t tx;
    app_in_t in;
    app_out_t out;
    ms_t now;
    bool efuse; /* pg_ok = out_ok = previous tick's vbus_en (a working eFuse with a load) */
    size_t rd;  /* read cursor into tx.buf */
} fx_t;

static fx_t F;
static char took[TXCAP];

static void fx_init_env(bool optr_err)
{
    memset(&F, 0, sizeof F);
    F.tx.space = 64;
    F.env.tx_space = fx_space;
    F.env.tx_write = fx_write;
    F.env.ctx = &F.tx;
    F.env.serial = "0123456789ABCDEF01234567";
    F.env.fw_version = "0.1.0";
    F.env.reset_token = "pin";
    F.env.boot_count = 3;
    F.env.optr_err = optr_err;
    F.efuse = true;
}

static void fx_start_at(ms_t t0)
{
    app_init(&F.a, &F.env, &F.in, t0);
    F.now = t0;
}

static void fx_start(void)
{
    fx_start_at(0);
}

static void fx_init(void)
{
    fx_init_env(false);
    fx_start();
}

static void tick(void)
{
    F.now++;
    if (F.efuse) {
        F.in.pg_ok = F.out.vbus_en;
        F.in.out_ok = F.out.vbus_en;
    }
    app_tick(&F.a, F.now, &F.in, &F.out);
}

static void tick_to(ms_t t)
{
    while (F.now < t) tick();
}

/* n ticks (wrap-safe, unlike tick_to). */
static void tick_n(unsigned n)
{
    while (n-- > 0u) tick();
}

static void ev(usb_ev_t e)
{
    app_usb_event(&F.a, e, F.now);
}

/* Feed bytes, honouring app_can_accept_rx (ticks while it is false, bounded). */
static void feed(const char *s)
{
    unsigned guard = 0;
    while (*s != '\0' && guard < 200000u) {
        if (app_can_accept_rx(&F.a)) {
            app_rx_byte(&F.a, *s++, F.now);
        } else {
            tick();
            guard++;
        }
    }
    CHECK(*s == '\0');
}

/* Send one command line now (rx must be accepted) and run ticks until the reply is complete. */
static void cmd(const char *line)
{
    CHECK(app_can_accept_rx(&F.a));
    for (const char *s = line; *s != '\0'; s++) app_rx_byte(&F.a, *s, F.now);
    app_rx_byte(&F.a, '\n', F.now);
    unsigned guard = 0;
    while (!app_can_accept_rx(&F.a) && guard++ < 100000u) tick();
}

/* All output written since the last take(). */
static const char *take(void)
{
    size_t n = F.tx.len - F.rd;
    memcpy(took, F.tx.buf + F.rd, n);
    took[n] = '\0';
    F.rd = F.tx.len;
    return took;
}

static const char *cmd_take(const char *line)
{
    take();
    cmd(line);
    return take();
}

static bool starts_with(const char *s, const char *p)
{
    return strncmp(s, p, strlen(p)) == 0;
}

static bool ends_with(const char *s, const char *p)
{
    size_t ls = strlen(s), lp = strlen(p);
    return ls >= lp && strcmp(s + ls - lp, p) == 0;
}

/* Board with the control USB configured at tick 10 and the passthrough ON (tick 100). */
static void fx_ready(void)
{
    fx_init();
    tick_to(10);
    ev(USBEV_CONFIGURED);
    tick_to(100);
}

/* ^ERR [0-9]+ [a-z_]+: .+\r\n$ (one line) */
static bool err_grammar(const char *s)
{
    if (!starts_with(s, "ERR ")) return false;
    s += 4;
    if (*s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') s++;
    if (*s++ != ' ') return false;
    if (!((*s >= 'a' && *s <= 'z') || *s == '_')) return false;
    while ((*s >= 'a' && *s <= 'z') || *s == '_') s++;
    if (*s++ != ':' || *s++ != ' ') return false;
    const char *h = s;
    while (*s != '\0' && *s != '\r' && *s != '\n') s++;
    if (s == h) return false;
    return strcmp(s, "\r\n") == 0;
}

/* Next space-separated word of *p (NUL-terminated in place), NULL at the end. */
static char *next_word(char **p)
{
    char *s = *p;
    while (*s == ' ') s++;
    if (*s == '\0') return NULL;
    char *w = s;
    while (*s != '\0' && *s != ' ') s++;
    if (*s != '\0') *s++ = '\0';
    *p = s;
    return w;
}

/* ---------- tests ---------- */

TEST(test_app_id_reply)
{
    fx_init();
    tick_to(5);
    CHECK_EQ_STR(cmd_take("ID?"), "OK model=BenchHand hw=r1 fw=0.1.0 proto=1 sn=0123456789ABCDEF01234567\r\n");
}

TEST(test_app_status_format)
{
    static const char *const keys[] = {"rly", "req", "btn", "presses", "left", "in", "usb", "usb_req",
                                       "data_req", "ext", "usb_btn", "usb_presses", "fault", "seq", "ready",
                                       "boot", "reset", "uptime", "suspends", "lp_err", "optr_err"};
    fx_ready();
    CHECK_EQ_STR(cmd_take("SET 2 ON"), "OK\r\n");
    tick_to(200);
    const char *r = cmd_take("STATUS?");
    CHECK(ends_with(r, "\r\n"));
    char line[600];
    snprintf(line, sizeof line, "%s", r);
    line[strlen(line) - 2] = '\0';
    CHECK(strchr(line, '\r') == NULL && strchr(line, '\n') == NULL);

    char *save = line;
    char *t = next_word(&save);
    CHECK_EQ_STR(t, "OK");
    size_t nk = sizeof keys / sizeof keys[0];
    for (size_t i = 0; i < nk; i++) {
        t = next_word(&save);
        CHECK(t != NULL);
        if (t == NULL) return;
        char *eq = strchr(t, '=');
        CHECK(eq != NULL && eq[1] != '\0');
        if (eq == NULL) return;
        *eq = '\0';
        const char *v = eq + 1;
        CHECK_EQ_STR(t, keys[i]);
        if (strcmp(t, "rly") == 0) CHECK_EQ_STR(v, "0,1,0,0,0");
        if (strcmp(t, "req") == 0) CHECK_EQ_STR(v, "0,1,0,0,0");
        if (strcmp(t, "btn") == 0) CHECK_EQ_STR(v, "0,0,0,0,0");
        if (strcmp(t, "presses") == 0) CHECK_EQ_STR(v, "0,0,0,0,0");
        if (strcmp(t, "left") == 0) CHECK_EQ_STR(v, "0,59900,0,0,0"); /* SET at 100, now 200 */
        if (strcmp(t, "in") == 0) CHECK_EQ_STR(v, "0,0,0,0");
        if (strcmp(t, "usb") == 0) {
            static const char *const ok[] = {"off", "discharge", "blocked", "powering", "no_input", "power",
                                             "on", "fault"};
            bool found = false;
            for (size_t k = 0; k < 8; k++) found = found || strcmp(v, ok[k]) == 0;
            CHECK(found);
            CHECK_EQ_STR(v, "on");
        }
        if (strcmp(t, "usb_req") == 0) CHECK_EQ_STR(v, "1");
        if (strcmp(t, "fault") == 0) CHECK_EQ_STR(v, "none");
        if (strcmp(t, "seq") == 0) CHECK_EQ_STR(v, "idle");
        if (strcmp(t, "ready") == 0) CHECK_EQ_STR(v, "1");
        if (strcmp(t, "boot") == 0) CHECK_EQ_STR(v, "3");
        if (strcmp(t, "reset") == 0) CHECK_EQ_STR(v, F.env.reset_token);
        if (strcmp(t, "uptime") == 0) CHECK_EQ_STR(v, "200");
        if (strcmp(t, "suspends") == 0) CHECK_EQ_STR(v, "0");
        if (strcmp(t, "optr_err") == 0) CHECK_EQ_STR(v, "0");
    }
    CHECK(next_word(&save) == NULL);
}

TEST(test_app_status_left_inf_and_seq_running)
{
    fx_ready();
    cmd("MAX_ON 1 0");
    cmd("SET 1 ON");
    cmd("SEQ DELAY 1000; DELAY 1000");
    tick_to(150);
    const char *r = cmd_take("STATUS?");
    CHECK(strstr(r, " left=inf,0,0,0,0 ") != NULL);
    CHECK(strstr(r, " seq=running:1/2 ") != NULL);
}

TEST(test_app_read_replies)
{
    fx_ready();
    CHECK_EQ_STR(cmd_take("MAX_ON 2 5000"), "OK\r\n");
    CHECK_EQ_STR(cmd_take("MAX_ON?"), "OK max_on=60000,5000,60000,60000,60000\r\n");

    F.in.in_high[0] = true;
    tick_to(F.now + 10);
    CHECK_EQ_STR(cmd_take("INPUT?"), "OK in=1,0,0,0 rise=1,0,0,0 fall=0,0,0,0\r\n");

    CHECK_EQ_STR(cmd_take("SEQ SET 1 ON; DELAY 3000; SET 1 OFF"), "OK steps=3 delay_ms=3000 async=0\r\n");
    tick_to(F.now + 3100);
    CHECK_EQ_STR(cmd_take("SEQ USB OFF; DELAY 100; USB ON"), "OK steps=3 delay_ms=100 async=1\r\n");
    CHECK_EQ_STR(cmd_take("HELP FOO"), "ERR 3 bad_argument: no such HELP topic, send HELP for the command list\r\n");
}

TEST(test_app_limit_errors)
{
    fx_ready();
    cmd("MAX_ON 1 1000");
    CHECK(starts_with(cmd_take("PULSE 1 2000"), "ERR 6 limit: "));
    CHECK(starts_with(cmd_take("SEQ SET 1 ON; DELAY 2000; SET 1 OFF"), "ERR 6 limit: "));
    tick_to(F.now + 5);
    CHECK_EQ_INT(F.a.relays.req[0], 0);
    CHECK_EQ_INT(F.out.relay_mask, 0);
    CHECK(strstr(cmd_take("STATUS?"), " req=0,0,0,0,0 ") != NULL);
    CHECK(strstr(cmd_take("STATUS?"), " seq=idle ") != NULL);
    CHECK_EQ_STR(cmd_take("PULSE 1 1000"), "OK\r\n"); /* equal to MAX_ON is accepted */
}

/* Split `s` into replies (each ends with a line whose first token is OK or ERR). Returns the count. */
static int split_replies(const char *s, char rep[][8192], int max)
{
    int n = 0;
    size_t cur = 0;
    while (*s != '\0' && n < max) {
        const char *e = strstr(s, "\r\n");
        if (e == NULL) break;
        size_t l = (size_t)(e - s) + 2u;
        memcpy(rep[n] + cur, s, l);
        cur += l;
        rep[n][cur] = '\0';
        if (starts_with(s, "OK") || starts_with(s, "ERR")) {
            n++;
            cur = 0;
        }
        s = e + 2;
    }
    return n;
}

static char reps[64][8192];

TEST(test_app_pipelined_commands_reply_in_order)
{
    fx_ready();
    take();
    feed("ID?\nSET 1 ON\nSTATUS?\nHELP\nID?\n");
    tick_to(F.now + 500);
    int n = split_replies(take(), reps, 64);
    CHECK_EQ_INT(n, 5);
    CHECK(starts_with(reps[0], "OK model=BenchHand "));
    CHECK_EQ_STR(reps[1], "OK\r\n");
    CHECK(starts_with(reps[2], "OK rly="));
    CHECK(starts_with(reps[3], "# BenchHand r1 - "));
    CHECK(ends_with(reps[3], "\r\nOK\r\n"));
    /* every HELP data line starts with "# " */
    const char *p = reps[3];
    int lines = 0;
    while (!starts_with(p, "OK")) {
        CHECK(starts_with(p, "# "));
        p = strstr(p, "\r\n") + 2;
        lines++;
    }
    CHECK(lines > 10);
    CHECK(starts_with(reps[4], "OK model=BenchHand "));
}

/* M19: 16-byte RX FIFO (bytes go to the app only while app_can_accept_rx), 200-byte pipelined burst. */
TEST(test_app_rx_fifo_burst_no_loss)
{
    static const char *const cmds[] = {"ID?", "INPUT?", "MAX_ON?", "SET 1 OFF", "STATUS?", "FOO", "HELP SET"};
    static const char *const want[] = {"OK model=", "OK in=", "OK max_on=", "OK\r\n", "OK rly=", "ERR 1 ",
                                       "# SET"};
    char burst[400];
    int order[64];
    int nc = 0;
    size_t bl = 0;
    while (bl < 200u) {
        int k = nc % 7;
        bl += (size_t)snprintf(burst + bl, sizeof burst - bl, "%s\n", cmds[k]);
        order[nc++] = k;
    }
    fx_ready();
    take();
    F.tx.space = 7; /* slow host too */
    char fifo[16];
    size_t fl = 0, src = 0;
    unsigned guard = 0;
    while ((src < bl || fl > 0 || !app_can_accept_rx(&F.a)) && guard++ < 100000u) {
        while (fl < sizeof fifo && src < bl) fifo[fl++] = burst[src++];
        while (fl > 0 && app_can_accept_rx(&F.a)) {
            app_rx_byte(&F.a, fifo[0], F.now);
            memmove(fifo, fifo + 1, --fl);
        }
        tick();
    }
    int n = split_replies(take(), reps, 64);
    CHECK_EQ_INT(n, nc);
    for (int i = 0; i < n && i < nc; i++) {
        CHECK(starts_with(reps[i], want[order[i]]));
        CHECK(ends_with(reps[i], "\r\n"));
    }
}

TEST(test_app_tx_stall_drops_reply)
{
    fx_ready();
    CHECK_EQ_STR(cmd_take("PULSE 1 500"), "OK\r\n"); /* at 100: deadline 600 */
    F.tx.space = 16;
    take();
    for (const char *s = "HELP\n"; *s; s++) app_rx_byte(&F.a, *s, F.now);
    tick_to(110);
    CHECK(F.tx.len > F.rd); /* streaming started */
    CHECK(!app_can_accept_rx(&F.a));
    F.tx.space = 0; /* from tick 111 = T */
    const ms_t T = 111;
    while (F.now < T + 999) {
        tick();
        CHECK(!app_can_accept_rx(&F.a));
        if (F.now == 599) CHECK_EQ_INT(F.out.relay_mask, 1);
        if (F.now == 600) CHECK_EQ_INT(F.out.relay_mask, 0); /* PULSE deadline kept during the stall */
    }
    tick(); /* T + 1000 */
    CHECK(app_can_accept_rx(&F.a));
    F.tx.space = 64;
    const char *r = cmd_take("ID?");
    CHECK(starts_with(r, "OK model=BenchHand "));
    CHECK(ends_with(r, "\r\n"));
}

TEST(test_app_wait_blocks_rx_and_returns_t)
{
    fx_init();
    tick_to(10);
    const ms_t T0 = 10;
    take();
    for (const char *s = "WAIT 1 HIGH 1000\n"; *s; s++) app_rx_byte(&F.a, *s, F.now);
    while (F.now < T0 + 301) {
        if (F.now + 1 == T0 + 300) F.in.in_high[0] = true;
        tick();
        CHECK(!app_can_accept_rx(&F.a));
        CHECK_EQ_STR(take(), "");
    }
    tick(); /* T0 + 302 */
    CHECK_EQ_STR(take(), "OK t=302\r\n");
    CHECK(app_can_accept_rx(&F.a));
}

TEST(test_app_wait_variants)
{
    fx_init();
    F.in.in_high[0] = true;
    fx_start();
    tick_to(10);
    CHECK_EQ_STR(cmd_take("WAIT 1 HIGH 1000"), "OK t=0\r\n");

    /* RISE on input 2 */
    ms_t T0 = F.now;
    take();
    for (const char *s = "WAIT 2 RISE 1000\n"; *s; s++) app_rx_byte(&F.a, *s, F.now);
    while (F.now < T0 + 201) {
        if (F.now + 1 == T0 + 200) F.in.in_high[1] = true;
        tick();
    }
    CHECK_EQ_STR(take(), "");
    tick();
    CHECK_EQ_STR(take(), "OK t=202\r\n");

    /* FALL on input 2 (now high) */
    tick_to(F.now + 20);
    T0 = F.now;
    for (const char *s = "WAIT 2 FALL 1000\n"; *s; s++) app_rx_byte(&F.a, *s, F.now);
    while (F.now < T0 + 51) {
        if (F.now + 1 == T0 + 50) F.in.in_high[1] = false;
        tick();
    }
    CHECK_EQ_STR(take(), "");
    tick();
    CHECK_EQ_STR(take(), "OK t=52\r\n");

    /* timeout: input 1 low */
    F.in.in_high[0] = false;
    tick_to(F.now + 20);
    T0 = F.now;
    for (const char *s = "WAIT 1 HIGH 500\n"; *s; s++) app_rx_byte(&F.a, *s, F.now);
    tick_to(T0 + 499);
    CHECK_EQ_STR(take(), "");
    CHECK(!app_can_accept_rx(&F.a));
    tick();
    const char *r = take();
    CHECK(starts_with(r, "ERR 5 timeout: "));
    CHECK(err_grammar(r));
    CHECK(app_can_accept_rx(&F.a));
}

TEST(test_app_wait_aborted_by_safety_event)
{
    static const usb_ev_t evs[] = {USBEV_BUS_RESET, USBEV_SUSPEND, USBEV_UNCONFIGURED};
    static const char *const want[] = {"ERR 11 aborted: bus_reset, state was reset\r\n",
                                       "ERR 11 aborted: suspend, state was reset\r\n",
                                       "ERR 11 aborted: unconfigured, state was reset\r\n"};
    for (int i = 0; i < 3; i++) {
        fx_ready();
        take();
        for (const char *s = "WAIT 1 HIGH 10000\n"; *s; s++) app_rx_byte(&F.a, *s, F.now);
        tick_to(F.now + 50);
        CHECK(!app_can_accept_rx(&F.a));
        ev(evs[i]);
        tick();
        CHECK_EQ_STR(take(), want[i]);
        CHECK(app_can_accept_rx(&F.a));
    }
}

static const char BUSY[] = "ERR 7 busy: SEQ running, send SEQ ABORT or wait\r\n";

TEST(test_app_wait_refused_during_seq)
{
    fx_ready();
    CHECK_EQ_STR(cmd_take("SEQ DELAY 1000"), "OK steps=1 delay_ms=1000 async=0\r\n");
    tick();
    CHECK_EQ_STR(cmd_take("WAIT 1 LOW 100"), BUSY);
    CHECK(app_can_accept_rx(&F.a));
}

TEST(test_app_busy_during_seq)
{
    static const char *const busy_cmds[] = {"SET 1 ON", "SET ALL OFF", "PULSE 1 100", "USB ON", "USB OFF",
                                            "USB DATA ON", "USB DATA OFF", "USB DISCONNECT 100", "MAX_ON 1 100",
                                            "WAIT 1 HIGH 100", "REBOOT", "BOOTLOADER", "SEQ DELAY 10"};
    fx_ready();
    cmd("SEQ DELAY 5000");
    tick();
    for (size_t i = 0; i < sizeof busy_cmds / sizeof busy_cmds[0]; i++) {
        CHECK_EQ_STR(cmd_take(busy_cmds[i]), BUSY);
    }
    CHECK_EQ_INT(app_request(&F.a), APP_RUN);
    /* read commands still work */
    CHECK(strstr(cmd_take("STATUS?"), " seq=running:1/1 ") != NULL);
    CHECK(starts_with(cmd_take("ID?"), "OK model="));
    CHECK(starts_with(cmd_take("INPUT?"), "OK in="));
    CHECK(starts_with(cmd_take("MAX_ON?"), "OK max_on=60000,"));
    CHECK(ends_with(cmd_take("HELP SEQ"), "\r\nOK\r\n"));
    CHECK(ends_with(cmd_take("?"), "\r\nOK\r\n"));
    CHECK_EQ_STR(cmd_take("SEQ ABORT"), "OK\r\n");
    CHECK(strstr(cmd_take("STATUS?"), " seq=aborted:abort ") != NULL);
    CHECK_EQ_STR(cmd_take("SEQ ABORT"), "OK seq=idle\r\n");
    CHECK_EQ_STR(cmd_take("SET 1 ON"), "OK\r\n");
}

static const char NOT_READY[] = "ERR 10 not_ready: relays are enabled after USB configuration\r\n";

TEST(test_app_not_ready_before_configured)
{
    fx_init();
    tick_to(10);
    CHECK_EQ_STR(cmd_take("SET 1 ON"), NOT_READY);
    CHECK_EQ_STR(cmd_take("PULSE 1 100"), NOT_READY);
    CHECK_EQ_STR(cmd_take("SEQ SET 1 ON"), NOT_READY);
    CHECK_EQ_STR(cmd_take("SEQ USB OFF; SET ALL ON"), NOT_READY);
    CHECK_EQ_STR(cmd_take("SET 1 OFF"), "OK\r\n");
    CHECK(strstr(cmd_take("STATUS?"), " ready=0 ") != NULL);
    /* CONFIGURED acts in the next tick */
    ev(USBEV_CONFIGURED);
    CHECK_EQ_STR(cmd_take("SET 1 ON"), NOT_READY);
    tick();
    CHECK(strstr(cmd_take("STATUS?"), " ready=1 ") != NULL);
    CHECK_EQ_STR(cmd_take("SET 1 ON"), "OK\r\n");
}

TEST(test_app_bus_reset_releases_relays)
{
    fx_ready(); /* now 100 */
    CHECK_EQ_STR(cmd_take("PULSE 1 10000"), "OK\r\n");
    F.in.btn_pressed[1] = true; /* raw from 101, debounced at 111 */
    tick_to(120);
    CHECK_EQ_STR(cmd_take("SEQ SET 3 ON; DELAY 5000; SET 3 OFF"), "OK steps=3 delay_ms=5000 async=0\r\n");
    tick_to(200);
    CHECK_EQ_INT(F.out.relay_mask, 0x07);
    CHECK(F.out.vbus_en && F.out.data_en);

    ev(USBEV_BUS_RESET);
    tick();
    CHECK_EQ_INT(F.out.relay_mask, 0);
    CHECK(F.out.vbus_en && F.out.data_en); /* passthrough keeps following usb_req */
    const char *r = cmd_take("STATUS?");
    CHECK(strstr(r, " seq=aborted:bus_reset ") != NULL);
    CHECK(strstr(r, " req=0,0,0,0,0 ") != NULL);
    CHECK(strstr(r, " btn=0,1,0,0,0 ") != NULL);
    CHECK(strstr(r, " ready=0 ") != NULL);

    tick_to(300);
    ev(USBEV_CONFIGURED);
    while (F.now < 500) {
        tick();
        CHECK_EQ_INT(F.out.relay_mask, 0); /* nothing restored, held button blocked */
        CHECK(F.out.vbus_en);
    }
    F.in.btn_pressed[1] = false; /* release from 501 -> 511 */
    tick_to(600);
    CHECK_EQ_INT(F.out.relay_mask, 0);
    F.in.btn_pressed[1] = true; /* new press from 601 -> 611 */
    tick_to(610);
    CHECK_EQ_INT(F.out.relay_mask, 0);
    tick();
    CHECK_EQ_INT(F.out.relay_mask, 0x02);
}

TEST(test_app_unconfigured_reason)
{
    fx_ready();
    cmd("SEQ SET 2 ON; DELAY 5000");
    tick_to(F.now + 20);
    CHECK_EQ_INT(F.out.relay_mask, 0x02);
    ev(USBEV_UNCONFIGURED);
    tick();
    CHECK_EQ_INT(F.out.relay_mask, 0);
    const char *r = cmd_take("STATUS?");
    CHECK(strstr(r, " seq=aborted:unconfigured ") != NULL);
    CHECK(strstr(r, " rly=0,0,0,0,0 ") != NULL);
}

static void held_button_variant(bool early_press)
{
    fx_init();
    if (early_press) {
        tick_to(49);
        F.in.btn_pressed[0] = true; /* raw from 50 */
        tick_to(100);
        ev(USBEV_CONFIGURED);
    } else {
        tick_to(10);
        ev(USBEV_CONFIGURED);
        tick_to(100);
        ev(USBEV_SUSPEND);
        tick_to(104);
        F.in.btn_pressed[0] = true; /* raw from 105 */
        tick_to(200);
        ev(USBEV_RESUME);
        ev(USBEV_CONFIGURED);
    }
    while (F.now < 709) {
        if (F.now + 1 == 600) F.in.btn_pressed[0] = false;
        if (F.now + 1 == 700) F.in.btn_pressed[0] = true;
        tick();
        if (F.now >= 105) CHECK_EQ_INT(F.out.relay_mask & 1, 0);
    }
    tick(); /* 710 */
    CHECK_EQ_INT(F.out.relay_mask & 1, 1);
}

TEST(test_app_held_button_blocked_when_allowed)
{
    held_button_variant(false);
    held_button_variant(true);
}

TEST(test_app_usb_button_held_at_startup_forces_off)
{
    fx_init_env(false);
    F.in.btn_pressed[BTN_USB] = true;
    fx_start();
    while (F.now < 509) {
        if (F.now + 1 == 500) F.in.btn_pressed[BTN_USB] = false;
        tick();
        CHECK_EQ_INT(F.out.vbus_en, 0);
    }
    tick();
    CHECK_EQ_INT(F.out.vbus_en, 1);
}

TEST(test_app_suspend_everything_off_then_resume)
{
    fx_ready();
    cmd("SET 1 ON");
    tick_to(150);
    CHECK_EQ_INT(F.out.relay_mask, 1);
    CHECK(F.out.led_usb && F.out.vbus_en);
    const ms_t T = F.now;
    ev(USBEV_SUSPEND);
    tick();
    CHECK_EQ_INT(F.out.relay_mask, 0);
    CHECK_EQ_INT(F.out.led_usb, 0);
    CHECK_EQ_INT(F.out.data_en, 0);
    CHECK_EQ_INT(app_request(&F.a), APP_RUN);
    while (F.now < T + 11) {
        tick();
        CHECK_EQ_INT(app_request(&F.a), APP_RUN);
        CHECK_EQ_INT(F.out.vbus_en, 0);
    }
    tick();
    CHECK_EQ_INT(app_request(&F.a), APP_REQ_STOP);
    app_stop_done(&F.a);
    ev(USBEV_RESUME);
    ev(USBEV_CONFIGURED);
    tick_to(F.now + 200);
    CHECK(F.out.vbus_en && F.out.data_en && F.out.led_usb); /* reconnect */
    CHECK_EQ_INT(F.out.relay_mask, 0);
    const char *r = cmd_take("STATUS?");
    CHECK(strstr(r, " suspends=1 ") != NULL);
    CHECK(strstr(r, " req=0,0,0,0,0 ") != NULL);
    CHECK(strstr(r, " ready=1 ") != NULL);
}

TEST(test_app_stop_request_one_shot)
{
    fx_ready();
    ev(USBEV_SUSPEND);
    tick_to(F.now + 30);
    CHECK_EQ_INT(app_request(&F.a), APP_REQ_STOP);
    CHECK_EQ_INT(app_request(&F.a), APP_REQ_STOP); /* until done */
    app_stop_done(&F.a);
    for (int i = 0; i < 100; i++) {
        tick();
        CHECK_EQ_INT(app_request(&F.a), APP_RUN);
    }
    ev(USBEV_SUSPEND);
    tick();
    CHECK_EQ_INT(app_request(&F.a), APP_REQ_STOP); /* passthrough already off */
    CHECK(strstr(cmd_take("STATUS?"), " suspends=2 ") != NULL);
}

static void resume_cancel_variant(int how)
{
    fx_init();
    tick_to(10);
    ev(USBEV_CONFIGURED);
    tick_to(100);
    ev(USBEV_SUSPEND);
    tick_to(105);
    if (how == 0) ev(USBEV_RESUME);
    if (how == 1) ev(USBEV_BUS_RESET);
    if (how == 2) ev(USBEV_CONFIGURED);
    while (F.now < 2000) {
        tick();
        CHECK_EQ_INT(app_request(&F.a), APP_RUN);
        if (how == 1 && F.now == 1000) ev(USBEV_CONFIGURED);
    }
    CHECK_EQ_STR(cmd_take("SET 1 ON"), "OK\r\n");
    for (int i = 0; i < 100; i++) {
        tick();
        CHECK_EQ_INT(app_request(&F.a), APP_RUN);
    }
    CHECK_EQ_INT(F.out.relay_mask, 1);
}

TEST(test_app_resume_cancels_stop)
{
    resume_cancel_variant(0);
    resume_cancel_variant(1);
    resume_cancel_variant(2);
}

static void bus_reset_during_suspend_variant(bool configured_only)
{
    fx_init();
    tick_to(10);
    ev(USBEV_CONFIGURED);
    tick_to(100);
    ev(USBEV_SUSPEND);
    tick_to(200);
    CHECK_EQ_INT(F.out.vbus_en, 0);
    ev(configured_only ? USBEV_CONFIGURED : USBEV_BUS_RESET);
    tick();
    CHECK_EQ_INT(F.out.vbus_en, 1);
    tick_to(300);
    if (!configured_only) {
        CHECK(strstr(cmd_take("STATUS?"), " ready=0 ") != NULL);
        ev(USBEV_CONFIGURED);
    }
    tick();
    CHECK(strstr(cmd_take("STATUS?"), " ready=1 ") != NULL);
    tick_to(400);
    CHECK_EQ_STR(cmd_take("SET 1 ON"), "OK\r\n");
    tick();
    CHECK_EQ_INT(F.out.relay_mask, 1);
    CHECK_EQ_INT(app_request(&F.a), APP_RUN);
}

TEST(test_app_bus_reset_during_suspend_recovers)
{
    bus_reset_during_suspend_variant(false);
    bus_reset_during_suspend_variant(true);
}

TEST(test_app_optr_err_never_requests_stop)
{
    fx_init_env(true);
    fx_start();
    tick_to(10);
    ev(USBEV_CONFIGURED);
    tick_to(100);
    cmd("SET 1 ON");
    tick_to(150);
    ev(USBEV_SUSPEND);
    for (int i = 0; i < 1000; i++) {
        tick();
        CHECK_EQ_INT(app_request(&F.a), APP_RUN);
        CHECK_EQ_INT(F.out.relay_mask, 0);
        CHECK_EQ_INT(F.out.led_usb, 0);
        CHECK_EQ_INT(F.out.data_en, 0);
        if (i >= 2) CHECK_EQ_INT(F.out.vbus_en, 0);
    }
    const char *r = cmd_take("STATUS?");
    CHECK(strstr(r, " lp_err=0 optr_err=1\r\n") != NULL);
    app_set_lp_err(&F.a);
    CHECK(strstr(cmd_take("STATUS?"), " lp_err=1 optr_err=1\r\n") != NULL);
}

static void reboot_variant(const char *c, app_request_t want)
{
    fx_ready();
    cmd("SET 1 ON");
    tick_to(150);
    CHECK(F.out.vbus_en && F.out.data_en);
    const ms_t T = F.now;
    take();
    for (const char *s = c; *s; s++) app_rx_byte(&F.a, *s, F.now);
    app_rx_byte(&F.a, '\n', F.now);
    CHECK(!app_can_accept_rx(&F.a));
    while (F.now < T + 11) {
        tick();
        CHECK_EQ_INT(F.out.relay_mask, 0);
        CHECK_EQ_INT(F.out.data_en, 0);
        if (F.now >= T + 2) CHECK_EQ_INT(F.out.vbus_en, 0);
        CHECK_EQ_STR(take(), "");
        CHECK_EQ_INT(app_request(&F.a), APP_RUN);
        CHECK(!app_can_accept_rx(&F.a));
    }
    tick(); /* T + 12: EN-low hold done, pt_is_off */
    CHECK_EQ_STR(take(), "OK\r\n");
    CHECK_EQ_INT(app_request(&F.a), want);
    tick();
    CHECK_EQ_INT(app_request(&F.a), want);
    CHECK_EQ_INT(F.out.relay_mask, 0);
    CHECK_EQ_INT(F.out.vbus_en, 0);
}

TEST(test_app_reboot_waits_for_en_low_hold)
{
    reboot_variant("REBOOT", APP_REQ_REBOOT);
    reboot_variant("BOOTLOADER", APP_REQ_BOOTLOADER);
}

TEST(test_app_reboot_ok_waits_for_tx)
{
    fx_ready();
    F.tx.space = 0;
    for (const char *s = "REBOOT\n"; *s; s++) app_rx_byte(&F.a, *s, F.now);
    tick_to(F.now + 100);
    CHECK_EQ_INT(app_request(&F.a), APP_RUN); /* OK not written yet */
    F.tx.space = 2;
    tick();
    CHECK_EQ_INT(app_request(&F.a), APP_RUN); /* "OK" written, CRLF pending */
    tick();
    CHECK_EQ_INT(app_request(&F.a), APP_REQ_REBOOT);
}

TEST(test_app_closing_ignores_pipelined_set)
{
    fx_ready();
    take();
    const char *s = "REBOOT\nSET 1 ON\n";
    unsigned guard = 0;
    while (app_request(&F.a) == APP_RUN && guard++ < 1000u) {
        while (*s != '\0' && app_can_accept_rx(&F.a)) app_rx_byte(&F.a, *s++, F.now);
        tick();
        CHECK_EQ_INT(F.out.relay_mask, 0);
    }
    CHECK_EQ_INT(app_request(&F.a), APP_REQ_REBOOT);
    CHECK_EQ_STR(take(), "OK\r\n");
    CHECK_EQ_INT(F.a.relays.req[0], 0);
}

TEST(test_app_echo_guard)
{
    fx_ready();
    take();
    feed("OK\nOK model=BenchHand hw=r1\nERR 1 unknown_command: send HELP for the command list\n"
         "# BenchHand r1\n#x\n   \n\r\n");
    tick_to(F.now + 20);
    CHECK_EQ_STR(take(), "");
    CHECK(app_can_accept_rx(&F.a));
}

TEST(test_app_unknown_hint_mentions_help)
{
    fx_init();
    tick_to(5);
    const char *r = cmd_take("FOO");
    CHECK(starts_with(r, "ERR 1 unknown_command: "));
    CHECK(ends_with(r, "send HELP for the command list\r\n"));
}

TEST(test_app_line_too_long)
{
    char line[402];
    memset(line, 'A', 400);
    line[400] = '\n';
    line[401] = '\0';
    fx_init();
    tick_to(5);
    take();
    feed(line);
    tick();
    const char *r = take();
    CHECK(starts_with(r, "ERR 9 line_too_long: "));
    CHECK(err_grammar(r));
    CHECK(starts_with(cmd_take("ID?"), "OK model=")); /* next line is fine */
}

TEST(test_app_help_topics)
{
    fx_init();
    tick_to(5);
    const char *r = cmd_take("help status?");
    CHECK(starts_with(r, "# STATUS?"));
    CHECK(ends_with(r, "\r\nOK\r\n"));
    CHECK(strstr(r, "optr_err") != NULL);
    r = cmd_take("? usb");
    CHECK(starts_with(r, "# USB "));
    CHECK(ends_with(r, "\r\nOK\r\n"));
    r = cmd_take("HELP USB typo"); /* Cx-m1 */
    CHECK(starts_with(r, "ERR 3 bad_argument: unexpected extra argument"));
    CHECK(err_grammar(r));
}

/* M15: every ERR code the app can emit (8 is reserved) matches the grammar. */
TEST(test_app_all_errors_match_grammar)
{
    int seen[12] = {0};
    const char *r;
    char line[402];

    fx_init();
    tick_to(5);
    r = cmd_take("FOO");
    CHECK(err_grammar(r)); seen[1] += starts_with(r, "ERR 1 unknown_command: ");
    r = cmd_take("SET 9 ON");
    CHECK(err_grammar(r)); seen[2] += starts_with(r, "ERR 2 bad_channel: ");
    r = cmd_take("SET 1 MAYBE");
    CHECK(err_grammar(r)); seen[3] += starts_with(r, "ERR 3 bad_argument: ");
    r = cmd_take("HELP NOPE");
    CHECK(err_grammar(r)); seen[3] += starts_with(r, "ERR 3 bad_argument: ");
    r = cmd_take("PULSE 1 0");
    CHECK(err_grammar(r)); seen[4] += starts_with(r, "ERR 4 out_of_range: ");
    r = cmd_take("WAIT 1 HIGH 5");
    CHECK(err_grammar(r)); seen[5] += starts_with(r, "ERR 5 timeout: ");
    r = cmd_take("SET 1 ON");
    CHECK(err_grammar(r)); seen[10] += starts_with(r, "ERR 10 not_ready: ");
    memset(line, 'x', 400);
    line[400] = '\0';
    r = cmd_take(line);
    CHECK(err_grammar(r)); seen[9] += starts_with(r, "ERR 9 line_too_long: ");

    ev(USBEV_CONFIGURED);
    tick_to(F.now + 5);
    cmd("MAX_ON 2 100");
    r = cmd_take("PULSE 2 101");
    CHECK(err_grammar(r)); seen[6] += starts_with(r, "ERR 6 limit: ");
    r = cmd_take("SEQ SET 2 ON; WAIT 1 HIGH 101");
    CHECK(err_grammar(r)); seen[6] += starts_with(r, "ERR 6 limit: ");
    cmd("SEQ DELAY 100");
    r = cmd_take("SET 1 ON");
    CHECK(err_grammar(r)); seen[7] += starts_with(r, "ERR 7 busy: ");
    cmd("SEQ ABORT");
    take();
    for (const char *s = "WAIT 1 HIGH 1000\n"; *s; s++) app_rx_byte(&F.a, *s, F.now);
    tick();
    ev(USBEV_BUS_RESET);
    tick();
    r = take();
    CHECK(err_grammar(r)); seen[11] += starts_with(r, "ERR 11 aborted: ");

    static const int codes[] = {1, 2, 3, 4, 5, 6, 7, 9, 10, 11};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        if (seen[codes[i]] == 0) printf("code %d not seen\n", codes[i]);
        CHECK(seen[codes[i]] > 0);
    }
}

TEST(test_app_usb_commands)
{
    fx_ready();
    CHECK(F.out.vbus_en && F.out.data_en);
    CHECK_EQ_STR(cmd_take("USB DATA OFF"), "OK\r\n");
    tick();
    CHECK(F.out.vbus_en && !F.out.data_en);
    CHECK(strstr(cmd_take("STATUS?"), " usb=power usb_req=1 data_req=0 ") != NULL);
    CHECK_EQ_STR(cmd_take("USB DATA ON"), "OK\r\n");
    tick();
    CHECK(F.out.data_en);
    CHECK_EQ_STR(cmd_take("USB OFF"), "OK\r\n");
    tick_to(F.now + 20);
    CHECK(!F.out.vbus_en && !F.out.data_en);
    CHECK_EQ_STR(cmd_take("USB ON"), "OK\r\n");
    tick_to(F.now + 50);
    CHECK(F.out.vbus_en && F.out.data_en);
    CHECK_EQ_STR(cmd_take("USB DISCONNECT 500"), "OK\r\n");
    tick_to(F.now + 100);
    CHECK(!F.out.vbus_en);
    tick_to(F.now + 500);
    CHECK(F.out.vbus_en && F.out.data_en);
}

TEST(test_app_suspend_cancels_disconnect)
{
    fx_ready();
    cmd("USB DISCONNECT 100000");
    tick_to(F.now + 50);
    CHECK(!F.out.vbus_en);
    ev(USBEV_SUSPEND);
    tick_to(F.now + 50);
    app_stop_done(&F.a);
    ev(USBEV_RESUME);
    tick_to(F.now + 100);
    CHECK(F.out.vbus_en && F.out.data_en); /* FW-09: the DISCONNECT timer ended at suspend */
}

/* Cx-I1: after init the passthrough keeps EN low for PT_EN_LOW_HOLD_MS even though the status pair is valid
 * (00) from tick 3. */
TEST(test_app_init_holds_passthrough_en_low)
{
    fx_init();
    while (F.now < PT_EN_LOW_HOLD_MS - 1u) {
        tick();
        CHECK_EQ_INT(F.out.vbus_en, 0);
    }
    tick(); /* PT_EN_LOW_HOLD_MS */
    CHECK_EQ_INT(F.out.vbus_en, 1);
}

/* Cx-I2: USB ON|OFF is the power request; data_req is independent and persists. */
TEST(test_app_usb_off_on_keeps_data_req)
{
    fx_ready();
    CHECK_EQ_STR(cmd_take("USB DATA OFF"), "OK\r\n");
    CHECK_EQ_STR(cmd_take("USB OFF"), "OK\r\n");
    tick_to(F.now + 30);
    CHECK(!F.out.vbus_en && !F.out.data_en);
    CHECK_EQ_STR(cmd_take("USB ON"), "OK\r\n");
    tick_to(F.now + 100);
    CHECK(F.out.vbus_en && !F.out.data_en);
    CHECK(strstr(cmd_take("STATUS?"), " usb=power usb_req=1 data_req=0 ") != NULL);
}

/* Review Focus 5: standalone WAIT (condition and timeout) across the 32-bit wrap. */
TEST(test_app_wait_across_wrap)
{
    const ms_t base = 0xFFFFFF00u;
    fx_init_env(false);
    fx_start_at(base);
    tick_n(10);
    take();
    for (const char *s = "WAIT 1 HIGH 1000\n"; *s; s++) app_rx_byte(&F.a, *s, F.now); /* T0 = base + 10 */
    for (unsigned i = 1; i <= 301; i++) {
        if (i == 300) F.in.in_high[0] = true; /* raw high from T0 + 300 (after the wrap), debounced at + 302 */
        tick();
        CHECK(!app_can_accept_rx(&F.a));
    }
    CHECK_EQ_STR(take(), "");
    tick();
    CHECK_EQ_STR(take(), "OK t=302\r\n");

    fx_init_env(false);
    fx_start_at(base);
    tick_n(10);
    take();
    for (const char *s = "WAIT 1 HIGH 500\n"; *s; s++) app_rx_byte(&F.a, *s, F.now);
    tick_n(499);
    CHECK_EQ_STR(take(), "");
    CHECK(!app_can_accept_rx(&F.a));
    tick(); /* T0 + 500 = 0x000000FE */
    CHECK(starts_with(take(), "ERR 5 timeout: "));
    CHECK(app_can_accept_rx(&F.a));
}

/* Review Focus 5: SEQ DELAY and SEQ WAIT (condition and timeout) across the 32-bit wrap. */
TEST(test_app_seq_across_wrap)
{
    const ms_t base = 0xFFFFFF00u;
    fx_init_env(false);
    fx_start_at(base);
    tick_n(10);
    ev(USBEV_CONFIGURED);
    tick_n(90); /* base + 100: 156 ms before the wrap */
    CHECK_EQ_STR(cmd_take("SEQ SET 1 ON; DELAY 300; SET 1 OFF"), "OK steps=3 delay_ms=300 async=0\r\n");
    const ms_t T0 = F.now;
    tick_n(2);
    for (unsigned i = 3; i <= 299; i++) {
        tick();
        CHECK_EQ_INT(F.out.relay_mask, 1);
    }
    tick_n(10);
    CHECK_EQ_INT(F.out.relay_mask, 0);
    CHECK(strstr(cmd_take("STATUS?"), " seq=done ") != NULL);
    CHECK(F.now - T0 < 0x80000000u && F.now < T0); /* really crossed the wrap */

    /* WAIT step: condition met after the wrap */
    fx_init_env(false);
    fx_start_at(base);
    tick_n(10);
    ev(USBEV_CONFIGURED);
    tick_n(90);
    CHECK(starts_with(cmd_take("SEQ WAIT 1 HIGH 400; SET 2 ON"), "OK steps=2 "));
    for (unsigned i = 1; i <= 200; i++) {
        if (i == 200) F.in.in_high[0] = true;
        tick();
        CHECK_EQ_INT(F.out.relay_mask, 0);
    }
    tick_n(20);
    CHECK_EQ_INT(F.out.relay_mask, 2);
    CHECK(strstr(cmd_take("STATUS?"), " seq=done ") != NULL);

    /* WAIT step: timeout after the wrap */
    fx_init_env(false);
    fx_start_at(base);
    tick_n(10);
    ev(USBEV_CONFIGURED);
    tick_n(90);
    CHECK(starts_with(cmd_take("SEQ WAIT 1 HIGH 300; SET 2 ON"), "OK steps=2 "));
    tick_n(290);
    CHECK(strstr(cmd_take("STATUS?"), " seq=running:1/2 ") != NULL);
    tick_n(20);
    CHECK(strstr(cmd_take("STATUS?"), " seq=aborted:timeout ") != NULL);
    CHECK_EQ_INT(F.out.relay_mask, 0);
}

/* Cl-m2: BUS_RESET / UNCONFIGURED discard a partial input line and a pending or streaming reply. */
TEST(test_app_bus_reset_discards_partial_line_and_reply)
{
    static const usb_ev_t evs[] = {USBEV_BUS_RESET, USBEV_UNCONFIGURED};
    for (int i = 0; i < 2; i++) {
        /* partial line */
        fx_ready();
        take();
        feed("SET 1 ");
        ev(evs[i]);
        tick();
        ev(USBEV_CONFIGURED);
        tick_to(F.now + 5);
        CHECK(starts_with(cmd_take("ID?"), "OK model=BenchHand "));

        /* streaming HELP */
        fx_ready();
        take();
        F.tx.space = 0;
        feed("HELP\n");
        CHECK(!app_can_accept_rx(&F.a));
        ev(evs[i]);
        F.tx.space = 64;
        tick_to(F.now + 50);
        CHECK_EQ_STR(take(), "");
        CHECK(app_can_accept_rx(&F.a));

        /* staged reply not yet written */
        fx_ready();
        take();
        F.tx.space = 0;
        feed("STATUS?\n");
        CHECK(!app_can_accept_rx(&F.a));
        ev(evs[i]);
        F.tx.space = 64;
        tick_to(F.now + 50);
        CHECK_EQ_STR(take(), "");
        CHECK(app_can_accept_rx(&F.a));
    }
}

/* Cl-m3: an echoed reply longer than 320 characters is dropped silently, not answered with ERR 9. */
TEST(test_app_echo_guard_long_line)
{
    static const char *const heads[] = {"OK ", "  err 9 x", "# data", "ok"};
    for (size_t h = 0; h < sizeof heads / sizeof heads[0]; h++) {
        char line[402];
        memset(line, 'x', 400);
        memcpy(line, heads[h], strlen(heads[h]));
        line[400] = '\n';
        line[401] = '\0';
        if (h == 3) line[2] = ' ';
        fx_ready();
        take();
        feed(line);
        tick_to(F.now + 20);
        CHECK_EQ_STR(take(), "");
        CHECK(starts_with(cmd_take("ID?"), "OK model="));
    }
}

/* Cl-m1: a wake that is not followed by bus activity (noise; a new SUSPEND within a few ms) never switches
 * VBUS on; a resume that holds switches it on after APP_RESUME_CONFIRM_MS. */
TEST(test_app_noise_wake_keeps_passthrough_off)
{
    fx_ready();
    CHECK(F.out.vbus_en);
    ev(USBEV_SUSPEND);
    tick_to(F.now + 30);
    CHECK_EQ_INT(app_request(&F.a), APP_REQ_STOP);
    for (int k = 0; k < 3; k++) {
        app_stop_done(&F.a);
        ev(USBEV_RESUME); /* noise wake: the bus stays idle, SUSP again after 3 ms */
        for (int i = 0; i < 4; i++) {
            tick();
            CHECK_EQ_INT(F.out.vbus_en, 0);
            CHECK_EQ_INT(F.out.data_en, 0);
        }
        ev(USBEV_SUSPEND);
        for (int i = 0; i < 30; i++) {
            tick();
            CHECK_EQ_INT(F.out.vbus_en, 0);
        }
        CHECK_EQ_INT(app_request(&F.a), APP_REQ_STOP);
    }
    /* real resume */
    app_stop_done(&F.a);
    ev(USBEV_RESUME);
    const ms_t T = F.now;
    while (F.now < T + APP_RESUME_CONFIRM_MS - 1u) {
        tick();
        CHECK_EQ_INT(F.out.vbus_en, 0);
    }
    tick_to(T + APP_RESUME_CONFIRM_MS + 40u);
    CHECK(F.out.vbus_en && F.out.data_en);
    CHECK_EQ_INT(app_request(&F.a), APP_RUN);
}

/* Resume hold time base (HIL PC-sleep finding): the hold counts from the RESUME event's own timestamp, not from
 * the last app_tick. The target loop can stall between the last tick and the event delivery (Stop path: debug UART
 * flush, SUSPRDY wait, clock restore), so the event time may be newer than the last tick's `now`; the core must
 * accept that (times only non-decreasing across app_tick / app_usb_event / app_rx_byte) and keep VBUS forced off
 * until event time + APP_RESUME_CONFIRM_MS. */
TEST(test_app_resume_hold_counts_from_event_time)
{
    fx_ready();
    CHECK(F.out.vbus_en);
    ev(USBEV_SUSPEND);
    tick_to(F.now + 30);
    CHECK_EQ_INT(app_request(&F.a), APP_REQ_STOP);
    app_stop_done(&F.a);
    const ms_t t_last_tick = F.now;
    const ms_t T = t_last_tick + 15u; /* stall of 15 ms without app_tick, then RESUME at T */
    F.now = T;
    ev(USBEV_RESUME);
    while (F.now < T + APP_RESUME_CONFIRM_MS - 1u) {
        tick();
        CHECK_EQ_INT(F.out.vbus_en, 0);
        CHECK_EQ_INT(F.out.data_en, 0);
    }
    tick_to(T + APP_RESUME_CONFIRM_MS + 40u);
    CHECK(F.out.vbus_en && F.out.data_en);
    CHECK_EQ_INT(app_request(&F.a), APP_RUN);
}

int main(void)
{
    RUN(test_app_id_reply);
    RUN(test_app_status_format);
    RUN(test_app_status_left_inf_and_seq_running);
    RUN(test_app_read_replies);
    RUN(test_app_limit_errors);
    RUN(test_app_pipelined_commands_reply_in_order);
    RUN(test_app_rx_fifo_burst_no_loss);
    RUN(test_app_tx_stall_drops_reply);
    RUN(test_app_wait_blocks_rx_and_returns_t);
    RUN(test_app_wait_variants);
    RUN(test_app_wait_aborted_by_safety_event);
    RUN(test_app_wait_refused_during_seq);
    RUN(test_app_busy_during_seq);
    RUN(test_app_not_ready_before_configured);
    RUN(test_app_bus_reset_releases_relays);
    RUN(test_app_unconfigured_reason);
    RUN(test_app_held_button_blocked_when_allowed);
    RUN(test_app_usb_button_held_at_startup_forces_off);
    RUN(test_app_suspend_everything_off_then_resume);
    RUN(test_app_stop_request_one_shot);
    RUN(test_app_resume_cancels_stop);
    RUN(test_app_resume_hold_counts_from_event_time);
    RUN(test_app_bus_reset_during_suspend_recovers);
    RUN(test_app_optr_err_never_requests_stop);
    RUN(test_app_reboot_waits_for_en_low_hold);
    RUN(test_app_reboot_ok_waits_for_tx);
    RUN(test_app_closing_ignores_pipelined_set);
    RUN(test_app_echo_guard);
    RUN(test_app_unknown_hint_mentions_help);
    RUN(test_app_line_too_long);
    RUN(test_app_help_topics);
    RUN(test_app_all_errors_match_grammar);
    RUN(test_app_usb_commands);
    RUN(test_app_suspend_cancels_disconnect);
    RUN(test_app_init_holds_passthrough_en_low);
    RUN(test_app_usb_off_on_keeps_data_req);
    RUN(test_app_wait_across_wrap);
    RUN(test_app_seq_across_wrap);
    RUN(test_app_bus_reset_discards_partial_line_and_reply);
    RUN(test_app_echo_guard_long_line);
    RUN(test_app_noise_wake_keeps_passthrough_off);
    return minitest_summary();
}

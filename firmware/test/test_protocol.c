/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/protocol.h"
#include "minitest.h"

static bool parse(const char *line, cmd_t *c, perr_t *e)
{
    memset(c, 0xA5, sizeof *c);
    memset(e, 0, sizeof *e);
    return proto_parse(line, c, e);
}

static int errcode(const char *line)
{
    cmd_t c;
    perr_t e;
    if (parse(line, &c, &e)) return -1;
    CHECK(e.hint != NULL && e.hint[0] != '\0');
    return (int)e.code;
}

TEST(test_parse_every_command_minimal_valid)
{
    cmd_t c;
    perr_t e;
    CHECK(parse("HELP", &c, &e)); CHECK_EQ_INT(c.kind, C_HELP); CHECK_EQ_STR(c.topic, "");
    CHECK(parse("HELP set", &c, &e)); CHECK_EQ_INT(c.kind, C_HELP); CHECK_EQ_STR(c.topic, "SET");
    CHECK(parse("ID?", &c, &e)); CHECK_EQ_INT(c.kind, C_ID);
    CHECK(parse("status?", &c, &e)); CHECK_EQ_INT(c.kind, C_STATUS);
    CHECK(parse("SET 3 on", &c, &e)); CHECK_EQ_INT(c.kind, C_SET); CHECK_EQ_INT(c.mask, 0x04); CHECK(c.on);
    CHECK(parse("set all off", &c, &e)); CHECK_EQ_INT(c.kind, C_SET); CHECK_EQ_INT(c.mask, 0x1F); CHECK(!c.on);
    CHECK(parse("PULSE 2 1000", &c, &e)); CHECK_EQ_INT(c.kind, C_PULSE); CHECK_EQ_INT(c.mask, 0x02); CHECK_EQ_INT(c.ms, 1000);
    CHECK(parse("MAX_ON 5 0", &c, &e)); CHECK_EQ_INT(c.kind, C_MAX_ON); CHECK_EQ_INT(c.mask, 0x10); CHECK_EQ_INT(c.ms, 0);
    CHECK(parse("MAX_ON ALL 3600000", &c, &e)); CHECK_EQ_INT(c.mask, 0x1F); CHECK_EQ_INT(c.ms, 3600000);
    CHECK(parse("MAX_ON?", &c, &e)); CHECK_EQ_INT(c.kind, C_MAX_ON_Q);
    CHECK(parse("INPUT?", &c, &e)); CHECK_EQ_INT(c.kind, C_INPUT_Q);
    CHECK(parse("WAIT 4 fall 60000", &c, &e)); CHECK_EQ_INT(c.kind, C_WAIT);
    CHECK_EQ_INT(c.in, 3); CHECK_EQ_INT(c.cond, COND_FALL); CHECK_EQ_INT(c.ms, 60000);
    CHECK(parse("WAIT 1 HIGH 1", &c, &e)); CHECK_EQ_INT(c.in, 0); CHECK_EQ_INT(c.cond, COND_HIGH);
    CHECK(parse("WAIT 2 low 5", &c, &e)); CHECK_EQ_INT(c.cond, COND_LOW);
    CHECK(parse("WAIT 2 RISE 5", &c, &e)); CHECK_EQ_INT(c.cond, COND_RISE);
    CHECK(parse("USB ON", &c, &e)); CHECK_EQ_INT(c.kind, C_USB); CHECK(c.on);
    CHECK(parse("USB off", &c, &e)); CHECK_EQ_INT(c.kind, C_USB); CHECK(!c.on);
    CHECK(parse("USB DATA ON", &c, &e)); CHECK_EQ_INT(c.kind, C_USB_DATA); CHECK(c.on);
    CHECK(parse("USB DATA OFF", &c, &e)); CHECK_EQ_INT(c.kind, C_USB_DATA); CHECK(!c.on);
    CHECK(parse("USB DISCONNECT 250", &c, &e)); CHECK_EQ_INT(c.kind, C_USB_DISCONNECT); CHECK_EQ_INT(c.ms, 250);
    CHECK(parse("SEQ DELAY 5", &c, &e)); CHECK_EQ_INT(c.kind, C_SEQ); CHECK_EQ_INT(c.seq.n, 1);
    CHECK(parse("SEQ ABORT", &c, &e)); CHECK_EQ_INT(c.kind, C_SEQ_ABORT);
    CHECK(parse("REBOOT", &c, &e)); CHECK_EQ_INT(c.kind, C_REBOOT);
    CHECK(parse("bootloader", &c, &e)); CHECK_EQ_INT(c.kind, C_BOOTLOADER);
    CHECK(parse("PULSE 1 3600000", &c, &e));
    CHECK(parse("WAIT 1 HIGH 60000", &c, &e));
}

TEST(test_echo_guard)
{
    cmd_t c;
    perr_t e;
    const char *ign[] = {"OK", "ok t=5", "ERR 7 busy: x", "# HELP line", "   #x", "Err", "\tOK"};
    for (unsigned i = 0; i < sizeof ign / sizeof ign[0]; i++) {
        CHECK(parse(ign[i], &c, &e));
        CHECK_EQ_INT(c.kind, C_IGNORE);
    }
    CHECK_EQ_INT(errcode("OKAY"), E_UNKNOWN);
    /* guard comes before the printable check */
    CHECK(parse("OK \x01", &c, &e)); CHECK_EQ_INT(c.kind, C_IGNORE);
}

TEST(test_question_mark_is_help)
{
    cmd_t c;
    perr_t e;
    CHECK(parse("?", &c, &e));
    CHECK_EQ_INT(c.kind, C_HELP);
    CHECK_EQ_STR(c.topic, "");
    CHECK(parse("  ?  ", &c, &e));
    CHECK_EQ_INT(c.kind, C_HELP);
}

TEST(test_help_topic)
{
    cmd_t c;
    perr_t e;
    CHECK(parse("HELP status?", &c, &e)); CHECK_EQ_STR(c.topic, "STATUS");
    CHECK(parse("help max_on?", &c, &e)); CHECK_EQ_STR(c.topic, "MAX_ON");
    CHECK(parse("HELP 123456789012345", &c, &e)); CHECK_EQ_STR(c.topic, "123456789012345");
    CHECK_EQ_INT(errcode("HELP 1234567890123456"), E_ARG);
    CHECK_EQ_INT(errcode("HELP abcdefghijklmnop?"), E_ARG);
    /* Cx-m1: whole-line validation, no third token */
    CHECK_EQ_INT(errcode("HELP USB typo"), E_ARG);
    CHECK_EQ_INT(errcode("? usb x"), E_ARG);
    CHECK_EQ_INT(errcode("help status? 1"), E_ARG);
    CHECK(parse("HELP USB  \t", &c, &e)); CHECK_EQ_STR(c.topic, "USB");
    CHECK(parse("HELP \t", &c, &e)); CHECK_EQ_STR(c.topic, "");
}

TEST(test_errors_codes_and_hints)
{
    CHECK_EQ_INT(errcode("FOO"), E_UNKNOWN);
    CHECK_EQ_INT(errcode("SET 6 ON"), E_CHANNEL);
    CHECK_EQ_INT(errcode("SET 0 ON"), E_CHANNEL);
    CHECK_EQ_INT(errcode("PULSE ALL 5"), E_CHANNEL);
    CHECK_EQ_INT(errcode("WAIT 5 HIGH 10"), E_CHANNEL);
    CHECK_EQ_INT(errcode("SET 1 MAYBE"), E_ARG);
    CHECK_EQ_INT(errcode("SET 1"), E_ARG);
    CHECK_EQ_INT(errcode("SET 1 ON extra"), E_ARG);
    CHECK_EQ_INT(errcode("PULSE 1 0"), E_RANGE);
    CHECK_EQ_INT(errcode("PULSE 1 3600001"), E_RANGE);
    CHECK_EQ_INT(errcode("MAX_ON 1 3600001"), E_RANGE);
    CHECK_EQ_INT(errcode("USB DISCONNECT 0"), E_RANGE);
    CHECK_EQ_INT(errcode("WAIT 1 HIGH 60001"), E_RANGE);
    CHECK_EQ_INT(errcode("WAIT 1 HIGH 0"), E_RANGE);
    CHECK_EQ_INT(errcode("WAIT 1 SIDEWAYS 5"), E_ARG);
    CHECK_EQ_INT(errcode("USB"), E_ARG);
    CHECK_EQ_INT(errcode("USB MAYBE"), E_ARG);
    CHECK_EQ_INT(errcode("SEQ"), E_ARG);
    CHECK_EQ_INT(errcode("REBOOT now"), E_ARG);
    CHECK_EQ_INT(errcode("ID? x"), E_ARG);
    {
        cmd_t c;
        perr_t e;
        CHECK(!parse("FOO", &c, &e));
        CHECK_EQ_STR(e.hint, "unknown command, send HELP for the command list");
        CHECK(!parse("SET \x80 ON", &c, &e));
        CHECK_EQ_INT(e.code, E_ARG);
        CHECK_EQ_STR(e.hint, "only printable ASCII is accepted");
        CHECK(!parse("SET 1\x7F", &c, &e));
        CHECK_EQ_INT(e.code, E_ARG);
    }
}

TEST(test_parse_numbers_and_whitespace)
{
    cmd_t c;
    perr_t e;
    CHECK_EQ_INT(errcode("PULSE 1 99999999999"), E_RANGE);
    CHECK_EQ_INT(errcode("PULSE 1 4294967296"), E_RANGE);
    CHECK_EQ_INT(errcode("PULSE 1 -5"), E_ARG);
    CHECK_EQ_INT(errcode("PULSE 1 +5"), E_ARG);
    CHECK_EQ_INT(errcode("PULSE 1 0x10"), E_ARG);
    CHECK_EQ_INT(errcode("PULSE 1 12ab"), E_ARG);
    CHECK(parse("PULSE 1 00100", &c, &e)); CHECK_EQ_INT(c.ms, 100);
    CHECK(parse("PULSE 1 00000000100", &c, &e)); CHECK_EQ_INT(c.ms, 100);
    CHECK_EQ_INT(errcode("PULSE 1 0000000000"), E_RANGE);
    CHECK(parse("PULSE\t1   100", &c, &e)); CHECK_EQ_INT(c.mask, 1); CHECK_EQ_INT(c.ms, 100);
    CHECK(parse("  \tPULSE 1 100 \t", &c, &e));
}

static void build_seq(char *out, int n)
{
    out[0] = '\0';
    strcat(out, "SEQ ");
    for (int i = 0; i < n; i++) {
        if (i) strcat(out, "; ");
        strcat(out, "SET 1 ON");
    }
}

TEST(test_seq_parse)
{
    cmd_t c;
    perr_t e;
    char line[512];
    CHECK(parse("SEQ SET 1 ON; SET 2 ON; DELAY 3000; SET 1 OFF; DELAY 100; SET 2 OFF", &c, &e));
    CHECK_EQ_INT(c.kind, C_SEQ);
    CHECK_EQ_INT(c.seq.n, 6);
    CHECK_EQ_INT(c.seq.step[0].kind, ST_SET); CHECK_EQ_INT(c.seq.step[0].mask, 1); CHECK(c.seq.step[0].on);
    CHECK_EQ_INT(c.seq.step[1].mask, 2);
    CHECK_EQ_INT(c.seq.step[2].kind, ST_DELAY); CHECK_EQ_INT(c.seq.step[2].ms, 3000);
    CHECK(!c.seq.step[3].on);
    CHECK_EQ_INT(c.seq.step[4].ms, 100);
    CHECK_EQ_INT(c.seq.step[5].kind, ST_SET);

    CHECK(parse("seq usb on;usb off ; USB DATA ON ;usb data off;WAIT 2 rise 50;set all on", &c, &e));
    CHECK_EQ_INT(c.seq.n, 6);
    CHECK_EQ_INT(c.seq.step[0].kind, ST_USB); CHECK(c.seq.step[0].on);
    CHECK_EQ_INT(c.seq.step[1].kind, ST_USB); CHECK(!c.seq.step[1].on);
    CHECK_EQ_INT(c.seq.step[2].kind, ST_USB_DATA); CHECK(c.seq.step[2].on);
    CHECK_EQ_INT(c.seq.step[3].kind, ST_USB_DATA); CHECK(!c.seq.step[3].on);
    CHECK_EQ_INT(c.seq.step[4].kind, ST_WAIT); CHECK_EQ_INT(c.seq.step[4].in, 1);
    CHECK_EQ_INT(c.seq.step[4].cond, COND_RISE); CHECK_EQ_INT(c.seq.step[4].ms, 50);
    CHECK_EQ_INT(c.seq.step[5].mask, 0x1F);

    build_seq(line, 16); CHECK(parse(line, &c, &e)); CHECK_EQ_INT(c.seq.n, 16);
    build_seq(line, 17); CHECK_EQ_INT(errcode(line), E_ARG);
    CHECK_EQ_INT(errcode("SEQ SET 1 ON;;DELAY 5"), E_ARG);
    CHECK_EQ_INT(errcode("SEQ SET 1 ON;"), E_ARG);
    CHECK_EQ_INT(errcode("SEQ ;SET 1 ON"), E_ARG);
    CHECK_EQ_INT(errcode("SEQ PULSE 1 5"), E_ARG);
    CHECK_EQ_INT(errcode("SEQ USB DISCONNECT 5"), E_ARG);
    CHECK_EQ_INT(errcode("SEQ SET 9 ON"), E_CHANNEL);
    CHECK_EQ_INT(errcode("SEQ DELAY 0"), E_RANGE);
    CHECK_EQ_INT(errcode("SEQ DELAY 3600001"), E_RANGE);
    CHECK_EQ_INT(errcode("SEQ WAIT 1 HIGH 60001"), E_RANGE);

    /* maximal line: 16 x "WAIT 4 FALL 60000" joined by ';' = 16*17 + 15 + 4 = 291.. pad with spaces to 306 */
    line[0] = '\0';
    strcat(line, "SEQ ");
    for (int i = 0; i < 16; i++) {
        if (i) strcat(line, "; ");
        strcat(line, "WAIT 4 FALL 60000");
    }
    CHECK_EQ_INT(strlen(line), 4 + 16 * 17 + 15 * 2);
    CHECK(parse(line, &c, &e)); CHECK_EQ_INT(c.seq.n, 16);
    CHECK_EQ_INT(c.seq.step[15].in, 3);
    {
        char buf[321];
        memset(buf, ' ', 320);
        memcpy(buf, line, strlen(line));
        buf[320] = '\0';
        CHECK(parse(buf, &c, &e)); CHECK_EQ_INT(c.seq.n, 16);
    }
}

TEST(test_seq_abort_vs_seq)
{
    cmd_t c;
    perr_t e;
    CHECK(parse("SEQ ABORT", &c, &e)); CHECK_EQ_INT(c.kind, C_SEQ_ABORT);
    CHECK(parse("seq   abort  ", &c, &e)); CHECK_EQ_INT(c.kind, C_SEQ_ABORT);
    CHECK_EQ_INT(errcode("SEQ ABORT now"), E_ARG);
    /* a second SEQ is just parsed */
    CHECK(parse("SEQ DELAY 5", &c, &e)); CHECK_EQ_INT(c.kind, C_SEQ);
}

TEST(test_err_reason)
{
    CHECK_EQ_STR(err_reason(E_UNKNOWN), "unknown_command");
    CHECK_EQ_STR(err_reason(E_CHANNEL), "bad_channel");
    CHECK_EQ_STR(err_reason(E_ARG), "bad_argument");
    CHECK_EQ_STR(err_reason(E_RANGE), "out_of_range");
    CHECK_EQ_STR(err_reason(E_TIMEOUT), "timeout");
    CHECK_EQ_STR(err_reason(E_LIMIT), "limit");
    CHECK_EQ_STR(err_reason(E_BUSY), "busy");
    CHECK_EQ_STR(err_reason(E_FAULT), "fault");
    CHECK_EQ_STR(err_reason(E_TOO_LONG), "line_too_long");
    CHECK_EQ_STR(err_reason(E_NOT_READY), "not_ready");
    CHECK_EQ_STR(err_reason(E_ABORTED), "aborted");
    CHECK_EQ_STR(err_reason((err_code_t)99), "unknown");
}

int main(void)
{
    RUN(test_parse_every_command_minimal_valid);
    RUN(test_echo_guard);
    RUN(test_question_mark_is_help);
    RUN(test_help_topic);
    RUN(test_errors_codes_and_hints);
    RUN(test_parse_numbers_and_whitespace);
    RUN(test_seq_parse);
    RUN(test_seq_abort_vs_seq);
    RUN(test_err_reason);
    return minitest_summary();
}

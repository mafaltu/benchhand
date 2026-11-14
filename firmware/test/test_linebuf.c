/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/linebuf.h"
#include "minitest.h"

/* Feed a string; collect events. Returns number of non-NONE events, keeps last event/line. */
typedef struct {
    int lines;
    int too_long;
    char last[LINEBUF_MAX + 2];
    int last_len;
} feed_t;

static void feed(linebuf_t *lb, feed_t *f, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const char *line = NULL;
        lb_event_t ev = linebuf_push(lb, s[i], &line);
        if (ev == LB_LINE) {
            f->lines++;
            f->last_len = (int)strlen(line);
            strcpy(f->last, line);
        } else if (ev == LB_TOO_LONG) {
            f->too_long++;
        }
    }
}

TEST(test_lf_cr_crlf_each_one_line)
{
    linebuf_t lb;
    feed_t f = {0};
    linebuf_init(&lb);
    feed(&lb, &f, "A\n", 2);
    CHECK_EQ_INT(f.lines, 1);
    CHECK_EQ_STR(f.last, "A");
    feed(&lb, &f, "B\r", 2);
    CHECK_EQ_INT(f.lines, 2);
    CHECK_EQ_STR(f.last, "B");
    feed(&lb, &f, "C\r\n", 3);
    CHECK_EQ_INT(f.lines, 3);
    CHECK_EQ_STR(f.last, "C");
    CHECK_EQ_INT(f.too_long, 0);
}

TEST(test_empty_lines_ignored)
{
    linebuf_t lb;
    feed_t f = {0};
    linebuf_init(&lb);
    feed(&lb, &f, "\r\n\r\n\n", 5);
    CHECK_EQ_INT(f.lines, 0);
    CHECK_EQ_INT(f.too_long, 0);
}

TEST(test_exactly_320_ok)
{
    linebuf_t lb;
    feed_t f = {0};
    char s[LINEBUF_MAX + 1];
    linebuf_init(&lb);
    memset(s, 'x', LINEBUF_MAX);
    s[LINEBUF_MAX] = '\n';
    feed(&lb, &f, s, sizeof s);
    CHECK_EQ_INT(f.lines, 1);
    CHECK_EQ_INT(f.last_len, 320);
    CHECK_EQ_INT(f.too_long, 0);
}

TEST(test_321_too_long_once)
{
    linebuf_t lb;
    feed_t f = {0};
    char s[LINEBUF_MAX + 2];
    linebuf_init(&lb);
    memset(s, 'x', LINEBUF_MAX + 1);
    s[LINEBUF_MAX + 1] = '\n';
    feed(&lb, &f, s, sizeof s);
    CHECK_EQ_INT(f.too_long, 1);
    CHECK_EQ_INT(f.lines, 0);
    feed(&lb, &f, "OK2\n", 4);
    CHECK_EQ_INT(f.too_long, 1);
    CHECK_EQ_INT(f.lines, 1);
    CHECK_EQ_STR(f.last, "OK2");
}

TEST(test_too_long_with_crlf_and_very_long)
{
    linebuf_t lb;
    feed_t f = {0};
    int i;
    linebuf_init(&lb);
    for (i = 0; i < 1000; i++) feed(&lb, &f, "y", 1);
    feed(&lb, &f, "\r\n", 2);
    CHECK_EQ_INT(f.too_long, 1);
    feed(&lb, &f, "Z\n", 2);
    CHECK_EQ_INT(f.lines, 1);
    CHECK_EQ_STR(f.last, "Z");
}

TEST(test_nonascii_byte_kept_for_parser)
{
    linebuf_t lb;
    const char *line = NULL;
    linebuf_init(&lb);
    CHECK_EQ_INT(linebuf_push(&lb, 'a', &line), LB_NONE);
    CHECK_EQ_INT(linebuf_push(&lb, (char)0xC3, &line), LB_NONE);
    CHECK_EQ_INT(linebuf_push(&lb, '\n', &line), LB_LINE);
    CHECK_EQ_INT(strlen(line), 2);
    CHECK_EQ_INT((unsigned char)line[1], 0xC3);
}

/* Cl-m3: an over-long echoed reply (OK / ERR first token or '#', case-insensitive, after blanks) is dropped
 * silently; any other over-long line still gives LB_TOO_LONG. */
static int too_long_of(const char *head)
{
    linebuf_t lb;
    feed_t f = {0};
    char s[401];
    linebuf_init(&lb);
    memset(s, 'x', 400);
    memcpy(s, head, strlen(head));
    s[400] = '\n';
    feed(&lb, &f, s, sizeof s);
    CHECK_EQ_INT(f.lines, 0);
    feed(&lb, &f, "ID?\n", 4); /* next line is fine */
    CHECK_EQ_INT(f.lines, 1);
    CHECK_EQ_STR(f.last, "ID?");
    return f.too_long;
}

TEST(test_too_long_echo_dropped)
{
    CHECK_EQ_INT(too_long_of("OK rly=0,1"), 0);
    CHECK_EQ_INT(too_long_of("ok "), 0);
    CHECK_EQ_INT(too_long_of("  \tERR 9 line_too_long: "), 0);
    CHECK_EQ_INT(too_long_of("Err\t"), 0);
    CHECK_EQ_INT(too_long_of("# USB-DEBUG"), 0);
    CHECK_EQ_INT(too_long_of("   #"), 0);
    CHECK_EQ_INT(too_long_of("OKAY "), 1);
    CHECK_EQ_INT(too_long_of("ERROR "), 1);
    CHECK_EQ_INT(too_long_of("SET 1 ON "), 1);
    CHECK_EQ_INT(too_long_of("x"), 1);
}

int main(void)
{
    RUN(test_lf_cr_crlf_each_one_line);
    RUN(test_empty_lines_ignored);
    RUN(test_exactly_320_ok);
    RUN(test_321_too_long_once);
    RUN(test_too_long_with_crlf_and_very_long);
    RUN(test_nonascii_byte_kept_for_parser);
    RUN(test_too_long_echo_dropped);
    return minitest_summary();
}

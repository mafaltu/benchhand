/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/linebuf.h"

void linebuf_init(linebuf_t *lb)
{
    lb->buf[0] = '\0';
    lb->len = 0;
    lb->overflow = false;
    lb->last_was_cr = false;
}

static char up(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* First token of the stored bytes is "OK" / "ERR" or the first non-blank byte is '#' (the echo guard of
 * proto_parse, FW-23, applied to the LINEBUF_MAX bytes kept from an over-long line; Cl-m3). */
static bool echo_head(const linebuf_t *lb)
{
    uint16_t i = 0;
    while (i < lb->len && (lb->buf[i] == ' ' || lb->buf[i] == '\t')) i++;
    if (i < lb->len && lb->buf[i] == '#') return true;
    static const char *const words[] = {"OK", "ERR"};
    for (unsigned w = 0; w < 2u; w++) {
        const char *k = words[w];
        uint16_t j = i;
        while (*k != '\0' && j < lb->len && up(lb->buf[j]) == *k) {
            k++;
            j++;
        }
        if (*k == '\0' && (j == lb->len || lb->buf[j] == ' ' || lb->buf[j] == '\t')) return true;
    }
    return false;
}

lb_event_t linebuf_push(linebuf_t *lb, char c, const char **line_out)
{
    bool was_cr = lb->last_was_cr;

    lb->last_was_cr = false;
    if (c == '\r' || c == '\n') {
        if (c == '\n' && was_cr) {
            return LB_NONE; /* LF of a CRLF pair */
        }
        lb->last_was_cr = (c == '\r');
        if (lb->overflow) {
            const bool echo = echo_head(lb);
            lb->overflow = false;
            lb->len = 0;
            lb->buf[0] = '\0';
            return echo ? LB_NONE : LB_TOO_LONG; /* an echoed long reply is dropped silently */
        }
        if (lb->len == 0) {
            return LB_NONE;
        }
        lb->buf[lb->len] = '\0';
        lb->len = 0; /* buf stays valid until the next push */
        if (line_out != 0) {
            *line_out = lb->buf;
        }
        return LB_LINE;
    }
    if (lb->overflow) {
        return LB_NONE;
    }
    if (lb->len >= LINEBUF_MAX) {
        lb->overflow = true;
        return LB_NONE;
    }
    lb->buf[lb->len++] = c;
    return LB_NONE;
}

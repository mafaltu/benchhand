/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_LINEBUF_H
#define CORE_LINEBUF_H
#include <stdbool.h>
#include <stdint.h>

/* Maximum payload bytes per line (CR/LF are never stored); the buffer holds one extra byte for NUL. */
#define LINEBUF_MAX 320

typedef enum { LB_NONE, LB_LINE, LB_TOO_LONG } lb_event_t;

typedef struct {
    char buf[LINEBUF_MAX + 1];
    uint16_t len;
    bool overflow;
    bool last_was_cr;
} linebuf_t;

void linebuf_init(linebuf_t *lb);

/* Feed one byte. CR, LF and CRLF each terminate exactly one line; empty lines give LB_NONE.
 * LB_LINE: *line_out points to the NUL-terminated line, valid until the next push.
 * LB_TOO_LONG: returned once, at the terminator of a line longer than LINEBUF_MAX, unless the line looks like
 * an echoed reply (first token OK / ERR or first non-blank byte '#', case-insensitive): then LB_NONE. */
lb_event_t linebuf_push(linebuf_t *lb, char c, const char **line_out);

#endif

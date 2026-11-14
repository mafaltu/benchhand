/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/protocol.h"

#include <string.h>

#define LINE_MAX_BYTES 320 /* = LINEBUF_MAX; kept local so the parser does not depend on linebuf */
#define TOPIC_MAX 15
#define MS_MAX 3600000u
#define WAIT_MAX 60000u
#define ALL_MASK 0x1Fu

static const char HINT_UNKNOWN[] = "unknown command, send HELP for the command list";
static const char HINT_PRINTABLE[] = "only printable ASCII is accepted";
static const char HINT_TOO_LONG[] = "line longer than 320 bytes";
static const char HINT_CHANNEL[] = "channel must be 1-5 (ALL only for SET and MAX_ON), input 1-4";
static const char HINT_NUMBER[] = "number must be decimal digits only";
static const char HINT_RANGE[] = "number out of range, send HELP <command> for the limits";
static const char HINT_MISSING[] = "missing argument, send HELP <command> for the syntax";
static const char HINT_EXTRA[] = "unexpected extra argument, send HELP <command> for the syntax";
static const char HINT_ONOFF[] = "expected ON or OFF";
static const char HINT_COND[] = "condition must be HIGH, LOW, RISE or FALL";
static const char HINT_USB[] = "expected USB ON|OFF, USB DATA ON|OFF or USB DISCONNECT <ms>";
static const char HINT_TOPIC[] = "help topic longer than 15 characters";
static const char HINT_SEQ_EMPTY[] = "SEQ needs steps separated by ';' (or SEQ ABORT), no empty step";
static const char HINT_SEQ_STEP[] = "SEQ step must be SET, USB ON|OFF, USB DATA ON|OFF, DELAY or WAIT";
static const char HINT_SEQ_MANY[] = "SEQ has more than 16 steps";

static bool fail(perr_t *err, err_code_t code, const char *hint)
{
    err->code = code;
    err->hint = hint;
    return false;
}

static bool is_blank(char c) { return c == ' ' || c == '\t'; }

/* Next blank-separated token of *p (NUL-terminated in place), or NULL at end of string. */
static char *next_tok(char **p)
{
    char *s = *p;
    while (is_blank(*s)) s++;
    if (*s == '\0') {
        *p = s;
        return NULL;
    }
    char *start = s;
    while (*s != '\0' && !is_blank(*s)) s++;
    if (*s != '\0') *s++ = '\0';
    *p = s;
    return start;
}

static bool at_end(char **p)
{
    char *s = *p;
    while (is_blank(*s)) s++;
    *p = s;
    return *s == '\0';
}

static bool no_extra(char **p, perr_t *err)
{
    return at_end(p) ? true : fail(err, E_ARG, HINT_EXTRA);
}

static bool all_digits(const char *t)
{
    if (*t == '\0') return false;
    for (; *t != '\0'; t++)
        if (*t < '0' || *t > '9') return false;
    return true;
}

/* Value of a digit-only token with leading zeros stripped; false if more than 10 significant digits. */
static bool digits_value(const char *t, uint64_t *v)
{
    while (*t == '0' && t[1] != '\0') t++;
    if (strlen(t) > 10) return false;
    uint64_t r = 0;
    for (; *t != '\0'; t++) r = r * 10u + (uint64_t)(*t - '0');
    *v = r;
    return true;
}

static bool parse_num(char **p, uint32_t lo, uint32_t hi, uint32_t *val, perr_t *err)
{
    char *t = next_tok(p);
    if (t == NULL) return fail(err, E_ARG, HINT_MISSING);
    if (!all_digits(t)) return fail(err, E_ARG, HINT_NUMBER);
    uint64_t v;
    if (!digits_value(t, &v) || v < lo || v > hi) return fail(err, E_RANGE, HINT_RANGE);
    *val = (uint32_t)v;
    return true;
}

/* Channel: relay 1..5 (and ALL when allow_all) -> mask; or input 1..4 (n_max = 4) -> *mask = index 0..3. */
static bool parse_chan(char **p, uint8_t n_max, bool allow_all, uint8_t *res, perr_t *err)
{
    char *t = next_tok(p);
    if (t == NULL) return fail(err, E_ARG, HINT_MISSING);
    if (allow_all && strcmp(t, "ALL") == 0) {
        *res = ALL_MASK;
        return true;
    }
    uint64_t v;
    if (!all_digits(t) || !digits_value(t, &v) || v < 1 || v > n_max) return fail(err, E_CHANNEL, HINT_CHANNEL);
    *res = (uint8_t)(n_max == 5 ? (1u << (v - 1)) : (v - 1));
    return true;
}

static bool parse_onoff(char **p, bool *on, perr_t *err)
{
    char *t = next_tok(p);
    if (t == NULL) return fail(err, E_ARG, HINT_MISSING);
    if (strcmp(t, "ON") == 0) *on = true;
    else if (strcmp(t, "OFF") == 0) *on = false;
    else return fail(err, E_ARG, HINT_ONOFF);
    return true;
}

static bool parse_cond(char **p, in_cond_t *cond, perr_t *err)
{
    char *t = next_tok(p);
    if (t == NULL) return fail(err, E_ARG, HINT_MISSING);
    if (strcmp(t, "HIGH") == 0) *cond = COND_HIGH;
    else if (strcmp(t, "LOW") == 0) *cond = COND_LOW;
    else if (strcmp(t, "RISE") == 0) *cond = COND_RISE;
    else if (strcmp(t, "FALL") == 0) *cond = COND_FALL;
    else return fail(err, E_ARG, HINT_COND);
    return true;
}

/* "<in> <cond> <ms>" */
static bool parse_wait_args(char **p, uint8_t *in, in_cond_t *cond, ms_t *ms, perr_t *err)
{
    uint32_t v;
    return parse_chan(p, 4, false, in, err) && parse_cond(p, cond, err) && parse_num(p, 1, WAIT_MAX, &v, err) &&
           ((*ms = v), true);
}

/* "<ch|ALL> ON|OFF" */
static bool parse_set_args(char **p, uint8_t *mask, bool *on, perr_t *err)
{
    return parse_chan(p, 5, true, mask, err) && parse_onoff(p, on, err);
}

static bool parse_step(char *seg, seq_step_t *st, perr_t *err)
{
    char *p = seg;
    char *t = next_tok(&p);
    uint32_t v;
    memset(st, 0, sizeof *st);
    if (t == NULL) return fail(err, E_ARG, HINT_SEQ_EMPTY);
    if (strcmp(t, "SET") == 0) {
        st->kind = ST_SET;
        if (!parse_set_args(&p, &st->mask, &st->on, err)) return false;
    } else if (strcmp(t, "USB") == 0) {
        char *save = p;
        char *u = next_tok(&p);
        if (u != NULL && strcmp(u, "DATA") == 0) {
            st->kind = ST_USB_DATA;
        } else {
            st->kind = ST_USB;
            p = save;
        }
        if (!parse_onoff(&p, &st->on, err)) return false;
    } else if (strcmp(t, "DELAY") == 0) {
        st->kind = ST_DELAY;
        if (!parse_num(&p, 1, MS_MAX, &v, err)) return false;
        st->ms = v;
    } else if (strcmp(t, "WAIT") == 0) {
        st->kind = ST_WAIT;
        if (!parse_wait_args(&p, &st->in, &st->cond, &st->ms, err)) return false;
    } else {
        return fail(err, E_ARG, HINT_SEQ_STEP);
    }
    return no_extra(&p, err);
}

static bool parse_seq(char *rest, cmd_t *out, perr_t *err)
{
    char *p = rest;
    /* "SEQ ABORT" is a command, not a step list. Peek on a copy: next_tok writes NULs into the string. */
    {
        char tmp[LINE_MAX_BYTES + 1];
        char *q = tmp;
        strncpy(tmp, rest, sizeof tmp - 1);
        tmp[sizeof tmp - 1] = '\0';
        char *t = next_tok(&q);
        if (t != NULL && strcmp(t, "ABORT") == 0) {
            if (!no_extra(&q, err)) return false;
            out->kind = C_SEQ_ABORT;
            return true;
        }
    }
    if (at_end(&p)) return fail(err, E_ARG, HINT_SEQ_EMPTY);
    out->kind = C_SEQ;
    for (;;) {
        char *semi = strchr(p, ';');
        if (semi != NULL) *semi = '\0';
        if (out->seq.n >= SEQ_MAX_STEPS) return fail(err, E_ARG, HINT_SEQ_MANY);
        if (!parse_step(p, &out->seq.step[out->seq.n], err)) return false;
        out->seq.n++;
        if (semi == NULL) break;
        p = semi + 1;
    }
    return true;
}

static bool first_tok_is(const char *s, const char *word)
{
    while (is_blank(*s)) s++;
    for (; *word != '\0'; word++, s++) {
        char c = *s;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c != *word) return false;
    }
    return *s == '\0' || is_blank(*s);
}

static bool echo_line(const char *line)
{
    const char *s = line;
    while (is_blank(*s)) s++;
    return *s == '#' || first_tok_is(line, "OK") || first_tok_is(line, "ERR");
}

bool proto_parse(const char *line, cmd_t *out, perr_t *err)
{
    char buf[LINE_MAX_BYTES + 1];
    size_t n = 0;

    memset(out, 0, sizeof *out);
    out->kind = C_IGNORE;

    if (echo_line(line)) return true;

    for (const char *s = line; *s != '\0'; s++, n++) {
        unsigned char c = (unsigned char)*s;
        if ((c < 0x20 || c > 0x7E) && c != '\t') return fail(err, E_ARG, HINT_PRINTABLE);
    }
    if (n > LINE_MAX_BYTES) return fail(err, E_TOO_LONG, HINT_TOO_LONG);
    for (size_t i = 0; i <= n; i++) {
        char c = line[i];
        buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }

    char *p = buf;
    char *cmd = next_tok(&p);
    if (cmd == NULL) return true; /* blank line: nothing to do */

    uint32_t v;
    if (strcmp(cmd, "HELP") == 0 || strcmp(cmd, "?") == 0) {
        char *t = next_tok(&p);
        out->kind = C_HELP;
        if (t != NULL) {
            size_t len = strlen(t);
            if (len > 0 && t[len - 1] == '?') t[--len] = '\0';
            if (len > TOPIC_MAX) return fail(err, E_ARG, HINT_TOPIC);
            memcpy(out->topic, t, len + 1);
        }
        return no_extra(&p, err); /* HELP <topic> <extra> -> ERR 3 (whole-line validation) */
    }
    if (strcmp(cmd, "ID?") == 0) { out->kind = C_ID; return no_extra(&p, err); }
    if (strcmp(cmd, "STATUS?") == 0) { out->kind = C_STATUS; return no_extra(&p, err); }
    if (strcmp(cmd, "MAX_ON?") == 0) { out->kind = C_MAX_ON_Q; return no_extra(&p, err); }
    if (strcmp(cmd, "INPUT?") == 0) { out->kind = C_INPUT_Q; return no_extra(&p, err); }
    if (strcmp(cmd, "REBOOT") == 0) { out->kind = C_REBOOT; return no_extra(&p, err); }
    if (strcmp(cmd, "BOOTLOADER") == 0) { out->kind = C_BOOTLOADER; return no_extra(&p, err); }
    if (strcmp(cmd, "SET") == 0) {
        out->kind = C_SET;
        return parse_set_args(&p, &out->mask, &out->on, err) && no_extra(&p, err);
    }
    if (strcmp(cmd, "PULSE") == 0) {
        out->kind = C_PULSE;
        if (!parse_chan(&p, 5, false, &out->mask, err) || !parse_num(&p, 1, MS_MAX, &v, err)) return false;
        out->ms = v;
        return no_extra(&p, err);
    }
    if (strcmp(cmd, "MAX_ON") == 0) {
        out->kind = C_MAX_ON;
        if (!parse_chan(&p, 5, true, &out->mask, err) || !parse_num(&p, 0, MS_MAX, &v, err)) return false;
        out->ms = v;
        return no_extra(&p, err);
    }
    if (strcmp(cmd, "WAIT") == 0) {
        out->kind = C_WAIT;
        return parse_wait_args(&p, &out->in, &out->cond, &out->ms, err) && no_extra(&p, err);
    }
    if (strcmp(cmd, "USB") == 0) {
        char *t = next_tok(&p);
        if (t == NULL) return fail(err, E_ARG, HINT_USB);
        if (strcmp(t, "ON") == 0 || strcmp(t, "OFF") == 0) {
            out->kind = C_USB;
            out->on = (t[1] == 'N');
        } else if (strcmp(t, "DATA") == 0) {
            out->kind = C_USB_DATA;
            if (!parse_onoff(&p, &out->on, err)) return false;
        } else if (strcmp(t, "DISCONNECT") == 0) {
            out->kind = C_USB_DISCONNECT;
            if (!parse_num(&p, 1, MS_MAX, &v, err)) return false;
            out->ms = v;
        } else {
            return fail(err, E_ARG, HINT_USB);
        }
        return no_extra(&p, err);
    }
    if (strcmp(cmd, "SEQ") == 0) return parse_seq(p, out, err);

    return fail(err, E_UNKNOWN, HINT_UNKNOWN);
}

const char *err_reason(err_code_t c)
{
    switch (c) {
    case E_OK: return "ok";
    case E_UNKNOWN: return "unknown_command";
    case E_CHANNEL: return "bad_channel";
    case E_ARG: return "bad_argument";
    case E_RANGE: return "out_of_range";
    case E_TIMEOUT: return "timeout";
    case E_LIMIT: return "limit";
    case E_BUSY: return "busy";
    case E_FAULT: return "fault";
    case E_TOO_LONG: return "line_too_long";
    case E_NOT_READY: return "not_ready";
    case E_ABORTED: return "aborted";
    }
    return "unknown";
}

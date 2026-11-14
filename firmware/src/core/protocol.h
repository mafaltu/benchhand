/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_PROTOCOL_H
#define CORE_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>

#include "core/errors.h"
#include "core/timebase.h"

typedef enum { COND_HIGH, COND_LOW, COND_RISE, COND_FALL } in_cond_t;
typedef enum { ST_SET, ST_USB, ST_USB_DATA, ST_DELAY, ST_WAIT } step_kind_t;

typedef struct {
    step_kind_t kind;
    uint8_t mask; /* ST_SET: relay bit0 = ch1, ALL = 0x1F */
    bool on;      /* ST_SET, ST_USB, ST_USB_DATA */
    uint8_t in;   /* ST_WAIT: 0..3 */
    in_cond_t cond;
    ms_t ms;      /* ST_DELAY, ST_WAIT */
} seq_step_t;

#define SEQ_MAX_STEPS 16
typedef struct {
    seq_step_t step[SEQ_MAX_STEPS];
    uint8_t n;
} seq_prog_t;

typedef enum {
    C_IGNORE, C_HELP, C_ID, C_STATUS, C_SET, C_PULSE, C_MAX_ON, C_MAX_ON_Q, C_INPUT_Q, C_WAIT,
    C_USB, C_USB_DATA, C_USB_DISCONNECT, C_SEQ, C_SEQ_ABORT, C_REBOOT, C_BOOTLOADER
} cmd_kind_t;

typedef struct {
    cmd_kind_t kind;
    uint8_t mask; /* relay bit0 = ch1; ALL = 0x1F */
    uint8_t in;   /* 0..3 */
    bool on;
    ms_t ms;
    in_cond_t cond;
    char topic[16]; /* HELP topic, upper-case, "" = general help */
    seq_prog_t seq;
} cmd_t;

typedef struct {
    err_code_t code;
    const char *hint; /* static string, never empty */
} perr_t;

/* Parse one line (NUL-terminated, <= 320 bytes). On success *out is fully initialised (C_IGNORE for echoed
 * OK/ERR/# lines). On failure returns false and fills *err. Whether a HELP topic exists is not checked here. */
bool proto_parse(const char *line, cmd_t *out, perr_t *err);

#endif

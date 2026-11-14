/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_ERRORS_H
#define CORE_ERRORS_H

/* Protocol error codes (spec 5.2); the numeric values are part of the wire protocol. */
typedef enum {
    E_OK = 0,
    E_UNKNOWN = 1,
    E_CHANNEL = 2,
    E_ARG = 3,
    E_RANGE = 4,
    E_TIMEOUT = 5,
    E_LIMIT = 6,
    E_BUSY = 7,
    E_FAULT = 8,
    E_TOO_LONG = 9,
    E_NOT_READY = 10,
    E_ABORTED = 11
} err_code_t;

/* Reason word for "ERR <code> <reason>: <hint>" ("unknown_command", "bad_channel", ...). Static string. */
const char *err_reason(err_code_t c);

#endif

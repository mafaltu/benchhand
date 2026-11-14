/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef TARGET_DBG_RING_H
#define TARGET_DBG_RING_H
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "core/app.h"

/* Hardware-free part of the debug log (FW-32): line formatting, 512-byte ring, whole-line drop with a deferred
 * "[dbg] dropped N" notice, bounded flush and the io change filter. dbg_uart.c supplies the port (USART1 + HAL
 * tick); the host test test_dbg_ring.c supplies a fake one. Linked into the target only with FW_DEBUG_UART=ON.
 * Single context (main loop), no locking. */

#define DBG_RING_SIZE   512u /* power of two */
#define DBG_LINE_MAX    128u /* formatted line incl. prefix and CRLF is at most DBG_LINE_MAX - 1 bytes */
#define DBG_NOTICE_MAX  48u  /* >= strlen("[4294967295] [dbg] dropped 4294967295\r\n") = 39 */

typedef struct {
    bool (*tx_ready)(void *ctx);           /* a byte can be written now (USART TXE) */
    void (*tx_write)(void *ctx, uint8_t b); /* write one byte (USART TDR) */
    bool (*tx_idle)(void *ctx);            /* last byte fully shifted out (USART TC) */
    uint32_t (*now_ms)(void *ctx);         /* ms tick (HAL_GetTick) */
    void *ctx;
} dbg_port_t;

typedef struct {
    const dbg_port_t *port;
    char ring[DBG_RING_SIZE];
    uint16_t head; /* free-running, masked on access */
    uint16_t tail;
    uint32_t dropped;
    char line[DBG_LINE_MAX]; /* formatting scratch */
    /* dbg_ring_io() snapshot */
    bool io_valid;
    uint32_t io_btn, io_in;
    bool io_pg, io_ok;
    app_out_t io_out;
} dbg_ring_t;

void dbg_ring_init(dbg_ring_t *r, const dbg_port_t *port);
uint32_t dbg_ring_used(const dbg_ring_t *r);
uint32_t dbg_ring_dropped(const dbg_ring_t *r); /* lines dropped and not yet reported */
/* Queues "[<now>] <text>\r\n" (text truncated to fit DBG_LINE_MAX); drops the line whole when it does not fit or a
 * drop notice is still pending (keeps order). */
void dbg_ring_vprintf(dbg_ring_t *r, const char *fmt, va_list ap);
void dbg_ring_printf(dbg_ring_t *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Writes bytes while tx_ready(); queues the drop notice once >= DBG_NOTICE_MAX bytes are free. Never waits. */
void dbg_ring_poll(dbg_ring_t *r);
/* Polls until the ring is empty and tx_idle(), at most max_ms by now_ms(). True = drained. */
bool dbg_ring_flush(dbg_ring_t *r, uint32_t max_ms);
/* One "io ..." line when in / out differ from the previous call (first call always prints). */
void dbg_ring_io(dbg_ring_t *r, const app_in_t *in, const app_out_t *out);

#endif

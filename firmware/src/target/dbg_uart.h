/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef TARGET_DBG_UART_H
#define TARGET_DBG_UART_H
#include <stdint.h>

#include "core/app.h"

/* Optional debug log on USART1 TX = PA9 (TC2050 PROG_TX, ST-Link V3SET VCP), FW-32. Bring-up builds only:
 * CMake -DFW_DEBUG_UART=ON defines FW_DEBUG_UART=1 and links dbg_uart.c + dbg_ring.c. The default build has no
 * console: every function below is an empty stub, PA9/PA10 stay analog (board.c), nothing is linked.
 *
 * All calls are main-loop context only (no locking). Output is non-blocking: dbg_printf() formats one line into a
 * 512-byte RAM ring, dbg_poll() moves bytes to USART1_TDR while TXE is set. A line that does not fit is dropped
 * whole and counted; "[dbg] dropped N" is queued once space returns. Every line: "[<ms tick>] <text>\r\n".
 *
 * Stub rule: with the flag off dbg_printf(...) expands to nothing, so its arguments are NOT evaluated; pass only
 * side-effect-free arguments. */

#define DBG_FLUSH_MS 60u /* dbg_flush() bound: 512 B at 115200 8N1 = 44.4 ms; far below the 500 ms IWDG period */

#if defined(FW_DEBUG_UART) && FW_DEBUG_UART

/* USART1 115200 8N1 TX only on PA9 (AF1). Call once right after clock_init() (PCLK = 48 MHz). */
void dbg_init(void);
/* Formats one line (tick prefix and CRLF added) into the ring; drops it whole when it does not fit. */
void dbg_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Moves ring bytes to TDR while TXE is set; never waits. Call once per main-loop pass. */
void dbg_poll(void);
/* Blocking drain until the ring is empty and TC = 1, at most max_ms (HAL tick; interrupts must be enabled). */
void dbg_flush(uint32_t max_ms);
/* Boot banner: version, reset token, boot count, optr_err, UID serial. */
void dbg_boot(const char *reset_token, uint32_t boot_count, const char *serial);
/* One line per USB event forwarded to the app by usb_glue.c. */
void dbg_usb_event(usb_ev_t ev);
/* One line when app_in_t or app_out_t differs from the previous call (first call always prints). */
void dbg_io(const app_in_t *in, const app_out_t *out);

#else /* production build: no console */

#define dbg_printf(...) ((void)0)

static inline void dbg_init(void) {}
static inline void dbg_poll(void) {}
static inline void dbg_flush(uint32_t max_ms)
{
    (void)max_ms;
}
static inline void dbg_boot(const char *reset_token, uint32_t boot_count, const char *serial)
{
    (void)reset_token;
    (void)boot_count;
    (void)serial;
}
static inline void dbg_usb_event(usb_ev_t ev)
{
    (void)ev;
}
static inline void dbg_io(const app_in_t *in, const app_out_t *out)
{
    (void)in;
    (void)out;
}

#endif

#endif

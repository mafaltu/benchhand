/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
/* Hardware-free debug log queue (FW-32); see dbg_ring.h. C99 (also built by the host test). */
#include "target/dbg_ring.h"

#include <stdio.h>

/* C99-compatible compile-time check (the host test builds with -std=c99). */
typedef char dbg_ring_size_pow2[((DBG_RING_SIZE & (DBG_RING_SIZE - 1u)) == 0u) ? 1 : -1];

void dbg_ring_init(dbg_ring_t *r, const dbg_port_t *port)
{
    r->port = port;
    r->head = 0u;
    r->tail = 0u;
    r->dropped = 0u;
    r->io_valid = false;
}

uint32_t dbg_ring_used(const dbg_ring_t *r)
{
    return (uint16_t)(r->head - r->tail);
}

uint32_t dbg_ring_dropped(const dbg_ring_t *r)
{
    return r->dropped;
}

static uint32_t ring_free(const dbg_ring_t *r)
{
    return DBG_RING_SIZE - dbg_ring_used(r);
}

static void ring_put(dbg_ring_t *r, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        r->ring[r->head & (DBG_RING_SIZE - 1u)] = r->line[i];
        r->head++;
    }
}

/* Formats "[tick] <text>\r\n" into r->line; returns the length (text truncated to fit). */
static uint32_t format_line(dbg_ring_t *r, const char *fmt, va_list ap)
{
    int n = snprintf(r->line, DBG_LINE_MAX, "[%lu] ", (unsigned long)r->port->now_ms(r->port->ctx));
    if (n < 0) {
        n = 0;
    }
    uint32_t len = (uint32_t)n;
    if (len < DBG_LINE_MAX - 2u) {
        const int m = vsnprintf(&r->line[len], DBG_LINE_MAX - 2u - len, fmt, ap);
        if (m > 0) {
            len += (uint32_t)m;
        }
    }
    if (len > DBG_LINE_MAX - 3u) {
        len = DBG_LINE_MAX - 3u; /* vsnprintf truncated: keep what was written (terminator excluded) */
    }
    r->line[len++] = '\r';
    r->line[len++] = '\n';
    return len;
}

static uint32_t format(dbg_ring_t *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static uint32_t format(dbg_ring_t *r, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const uint32_t len = format_line(r, fmt, ap);
    va_end(ap);
    return len;
}

/* Queues the pending drop notice if it fits; true when no notice is pending afterwards. */
static bool drop_notice(dbg_ring_t *r)
{
    if (r->dropped == 0u) {
        return true;
    }
    const uint32_t len = format(r, "[dbg] dropped %lu", (unsigned long)r->dropped);
    if (len > ring_free(r)) {
        return false;
    }
    ring_put(r, len);
    r->dropped = 0u;
    return true;
}

void dbg_ring_vprintf(dbg_ring_t *r, const char *fmt, va_list ap)
{
    if (!drop_notice(r)) {
        r->dropped++; /* keep order: nothing new before the pending notice */
        return;
    }
    const uint32_t len = format_line(r, fmt, ap);
    if (len > ring_free(r)) {
        r->dropped++;
        return;
    }
    ring_put(r, len);
}

void dbg_ring_printf(dbg_ring_t *r, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    dbg_ring_vprintf(r, fmt, ap);
    va_end(ap);
}

void dbg_ring_poll(dbg_ring_t *r)
{
    const dbg_port_t *p = r->port;
    while (r->head != r->tail && p->tx_ready(p->ctx)) {
        p->tx_write(p->ctx, (uint8_t)r->ring[r->tail & (DBG_RING_SIZE - 1u)]);
        r->tail++;
    }
    if (r->dropped != 0u && ring_free(r) >= DBG_NOTICE_MAX) {
        (void)drop_notice(r); /* formats only once enough space is back, not on every pass */
    }
}

bool dbg_ring_flush(dbg_ring_t *r, uint32_t max_ms)
{
    const dbg_port_t *p = r->port;
    const uint32_t t0 = p->now_ms(p->ctx);
    for (;;) {
        dbg_ring_poll(r);
        if (r->head == r->tail && p->tx_idle(p->ctx)) {
            return true;
        }
        if ((uint32_t)(p->now_ms(p->ctx) - t0) >= max_ms) {
            return false;
        }
    }
}

static uint32_t pack(const bool *v, unsigned n)
{
    uint32_t m = 0u;
    for (unsigned i = 0; i < n; i++) {
        if (v[i]) {
            m |= 1u << i;
        }
    }
    return m;
}

void dbg_ring_io(dbg_ring_t *r, const app_in_t *in, const app_out_t *out)
{
    const uint32_t btn = pack(in->btn_pressed, BTN_N);
    const uint32_t inp = pack(in->in_high, IN_N);
    if (r->io_valid && btn == r->io_btn && inp == r->io_in && in->pg_ok == r->io_pg && in->out_ok == r->io_ok &&
        out->relay_mask == r->io_out.relay_mask && out->vbus_en == r->io_out.vbus_en &&
        out->data_en == r->io_out.data_en && out->led_usb == r->io_out.led_usb) {
        return;
    }
    r->io_valid = true;
    r->io_btn = btn;
    r->io_in = inp;
    r->io_pg = in->pg_ok;
    r->io_ok = in->out_ok;
    r->io_out = *out;
    /* btn: bit i = button i+1 pressed (bit 5 = BTN_USB); in: bit i = IN(i+1) HIGH; pg/ok: passthrough status;
     * rly: relay_mask bit0 = RLY1; vbus/data/led: outputs. */
    dbg_ring_printf(r, "io btn=%02lX in=%lX pg=%d ok=%d | rly=%02X vbus=%d data=%d led=%d", (unsigned long)btn,
                    (unsigned long)inp, in->pg_ok ? 1 : 0, in->out_ok ? 1 : 0, (unsigned)out->relay_mask,
                    out->vbus_en ? 1 : 0, out->data_en ? 1 : 0, out->led_usb ? 1 : 0);
}

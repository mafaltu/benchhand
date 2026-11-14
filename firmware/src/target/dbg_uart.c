/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
/* Optional debug log on USART1 TX (FW-32). Compiled only with -DFW_DEBUG_UART=ON; see dbg_uart.h.
 *
 * References:
 *   - PA9 AF1 = USART1_TX: DS14693 Rev 1 Table 13 "Port A alternate function mapping (AF0 to AF7)", p. 42.
 *   - AN2606: PA9/PA10 are also the ROM system memory boot loader USART1 pins. Configuring PA9 as AF here is
 *     harmless: the ROM loader is not running while the application runs, and a BOOTLOADER jump starts from a
 *     reset (all pins back to their reset state).
 *   - RCC_APBENR2.USART1EN bit 14 (RM0490 Rev 6 6.4.15, p. 153); RCC_CCIPR.USART1SEL = 00 -> PCLK (6.4.20,
 *     p. 158). PCLK = HCLK = SYSCLK = 48 MHz (clock.c: HSIDIV 1, SYSDIV 1, HPRE 1, PPRE 1).
 *   - Baud rate (26.5.7): OVER8 = 0, USART_PRESC = 0 (/1, reset value): BRR = USARTDIV = 48 MHz / 115200 = 416.67
 *     -> 417, actual 115108 Bd, error -0.08 %.
 *   - CR1 with FIFOEN = 0 (reset): ISR bit 7 = TXE, bit 6 = TC (26.8.9).
 *   - Low-power (26.6, Table 132, p. 793): USART1 is a FULL instance (Table 124, p. 745, wake-up capable), so it
 *     need not be disabled before Stop; Stop 0 keeps all registers. Its kernel clock (PCLK) stops in Stop and
 *     is back at 48 MHz after clock_restore_after_stop(), before any dbg call after the wake, so no re-init is
 *     needed. The ring is flushed (dbg_flush) before Stop entry, so no character is cut by the clock stop.
 *
 * TX only: PA10 (PROG_RX) stays analog as set by board.c.
 * Queue, formatting, drop notice, flush loop and io filter: dbg_ring.c (hardware-free, host test test_dbg_ring.c).
 * This file is only the USART1 port and the log helpers. */
#include "target/dbg_uart.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "core/build_info.h"
#include "stm32c0xx.h"
#include "stm32c0xx_hal.h"
#include "target/boot.h"
#include "target/dbg_ring.h"

#define DBG_BAUD    115200u
#define DBG_PCLK_HZ 48000000u
#define PA9_AF_USART1_TX 1u

static bool port_tx_ready(void *ctx)
{
    (void)ctx;
    return (USART1->ISR & USART_ISR_TXE_TXFNF) != 0u;
}

static void port_tx_write(void *ctx, uint8_t b)
{
    (void)ctx;
    USART1->TDR = b;
}

static bool port_tx_idle(void *ctx)
{
    (void)ctx;
    return (USART1->ISR & USART_ISR_TC) != 0u;
}

static uint32_t port_now_ms(void *ctx)
{
    (void)ctx;
    return HAL_GetTick();
}

static const dbg_port_t s_port = {
    .tx_ready = port_tx_ready,
    .tx_write = port_tx_write,
    .tx_idle = port_tx_idle,
    .now_ms = port_now_ms,
    .ctx = NULL,
};

static dbg_ring_t s_dbg;

void dbg_init(void)
{
    RCC->APBENR2 |= RCC_APBENR2_USART1EN;
    (void)RCC->APBENR2;
    RCC->CCIPR &= ~RCC_CCIPR_USART1SEL; /* PCLK (reset value, written explicitly) */

    USART1->CR1 = 0u; /* UE = 0: 8 data bits (M = 00), no parity, OVER8 = 0, FIFO off */
    USART1->CR2 = 0u; /* 1 stop bit */
    USART1->CR3 = 0u;
    USART1->PRESC = 0u;
    USART1->BRR = (DBG_PCLK_HZ + DBG_BAUD / 2u) / DBG_BAUD;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE; /* TX drives the idle level (high) from here */

    /* PA9: push-pull, low speed, no pull, AF1, then MODER 11 (analog, board.c) -> 10 (AF). */
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT9;
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED9;
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD9;
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~GPIO_AFRH_AFSEL9) | (PA9_AF_USART1_TX << GPIO_AFRH_AFSEL9_Pos);
    GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODE9) | (2u << GPIO_MODER_MODE9_Pos);

    dbg_ring_init(&s_dbg, &s_port);
}

void dbg_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    dbg_ring_vprintf(&s_dbg, fmt, ap);
    va_end(ap);
}

void dbg_poll(void)
{
    dbg_ring_poll(&s_dbg);
}

void dbg_flush(uint32_t max_ms)
{
    (void)dbg_ring_flush(&s_dbg, max_ms);
}

void dbg_boot(const char *reset_token, uint32_t boot_count, const char *serial)
{
    dbg_printf("BenchHand fw %s reset=%s boot=%lu optr_err=%d uid=%s", FW_VERSION, reset_token,
               (unsigned long)boot_count, boot_optr_iwdg_stop_ok() ? 0 : 1, serial);
}

void dbg_usb_event(usb_ev_t ev)
{
    static const char *const names[] = {"configured", "unconfigured", "bus_reset", "suspend", "resume"};
    const unsigned i = (unsigned)ev;
    dbg_printf("usb %s", i < sizeof names / sizeof names[0] ? names[i] : "?");
}

void dbg_io(const app_in_t *in, const app_out_t *out)
{
    dbg_ring_io(&s_dbg, in, out);
}

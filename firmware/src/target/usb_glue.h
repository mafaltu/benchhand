/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef TARGET_USB_GLUE_H
#define TARGET_USB_GLUE_H
#include <stdbool.h>
#include <stddef.h>

#include "core/app.h"

/* USB CDC glue between TinyUSB 0.21.0 and the app (usb_glue.c). Everything except the IRQ handler runs in
 * main-loop context; USB events reach the app only through app_usb_event() from here, never from an ISR. */

/* Pulses RCC_APBRSTR1.USBRST (clean peripheral after a ROM jump, Q-2), enables the USB clock and starts TinyUSB
 * (pull-up on). Call once after clock_init(). */
void usb_glue_init(void);

/* Runs tud_task(), then delivers a pending bus reset and flushes the CDC TX FIFO. TinyUSB callbacks
 * (mount/umount/suspend/resume) fire inside it and are delivered to `app` with `now`: pass the current
 * tick (HAL_GetTick()), not the time of an earlier app_tick, so event timers (RESUME hold, SUSPEND stop wait) start
 * at the delivery. */
void usb_glue_task(app_t *app, ms_t now);

/* app_env_t TX callbacks: free bytes in the CDC TX FIFO / write (at most the free space) and flush.
 * Independent of DTR (FW-23). */
size_t usb_glue_tx_space(void *ctx);
void usb_glue_tx_write(void *ctx, const char *s, size_t n);

/* Next received byte, false when none is pending. */
bool usb_glue_rx_byte(char *c);

/* Delivers a bus reset latched by the ISR hook as app_usb_event(USBEV_BUS_RESET) to the app last passed to
 * usb_glue_task(). Called after every tud_task() and first in every TinyUSB state callback, so a CONFIGURED of a
 * re-enumeration always follows its BUS_RESET. No-op when no bus reset is pending. */
void usb_glue_deliver_bus_reset(void);

/* True when the CDC TX FIFO is empty and the CDC IN endpoint has no transfer in flight (the last packet was
 * ACKed by the host). Task 12 (REBOOT / BOOTLOADER, m-2). */
bool usb_glue_tx_drained(void);

/* Close path only (REBOOT / BOOTLOADER): tud_task() + TX flush. TinyUSB callbacks still reach the app last passed
 * to usb_glue_task(); its outputs are already off and no longer written. */
void usb_glue_service(void);

/* Close path only: tud_disconnect(), the host sees a detach. */
void usb_glue_detach(void);

#endif

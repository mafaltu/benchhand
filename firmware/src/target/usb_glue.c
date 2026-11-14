/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
/* USB CDC glue: TinyUSB 0.21.0 (lib/tinyusb, FSDEV driver portable/st/stm32_fsdev) <-> app (spec 4.3, 5.1, FW-23).
 * Replaces the Task 9 stub (usb_stub.c, renamed). */
#include "target/usb_glue.h"

#include <stdbool.h>
#include <stdint.h>

#include "stm32c0xx_hal.h"
#include "tusb.h"
#include "device/dcd.h" /* dcd_eventid_t (dcd.h:42-53) for tud_event_hook_cb */
#include "device/usbd_pvt.h" /* usbd_edpt_busy (usbd_pvt.h:102, usbd.c:1644) */
#include "target/dbg_uart.h"
#include "target/usb_ids.h"

/* ---------- ES0618 2.11.1 guard (m-4) ----------
 * The TinyUSB delay before reading the PMA of an OUT transfer is compiled only for FSDEV 32-bit devices
 * (dcd_stm32_fsdev.c:447); CFG_TUSB_FSDEV_32BIT comes from tusb_option.h:350 (PMA 2048 B, tusb_mcu.h:207). */
#ifndef CFG_TUSB_FSDEV_32BIT
#error "ES0618 2.11.1: CFG_TUSB_FSDEV_32BIT not defined, the TinyUSB CTR_RX delay is not compiled in"
#endif
/* 11 cycles per loop (fsdev_stm32.h:301-311) at the 48 MHz SYSCLK of clock.c: count * 11 / 48 MHz >= 800 ns. */
_Static_assert(CFG_TUSB_FSDEV_BTABLE_FS_DELAY_COUNT * 11u * 1000000000ull / 48000000u >= 800u, "ES0618");
_Static_assert(CFG_TUD_CDC_TX_OVERWRITABLE_IF_NOT_CONNECTED == 0, "M-3: TX must not depend on DTR");

/* ---------- state ---------- */

/* Set in ISR context by tud_event_hook_cb, taken with IRQs masked in usb_glue_deliver_bus_reset(). */
static volatile bool s_bus_reset;
/* App and time of the current usb_glue_task() pass, for the callbacks fired inside tud_task(). */
static app_t *s_app;
static ms_t s_now;
/* CONFIGURED told to the app and not yet withdrawn by UNCONFIGURED / BUS_RESET. */
static bool s_app_configured;

/* ---------- IRQ ---------- */

/* Vector name of startup_stm32c071xx.s (lib/tinyusb/hw/bsp/stm32c0/family.c:49-51 uses the same). */
void USB_DRD_FS_IRQHandler(void)
{
    tud_int_handler(0);
}

/* TinyUSB 0.21.0 has no bus-reset callback: usbd.c:717-720 only runs usbd_reset(). The weak hook (usbd.c:48)
 * is called by queue_event() (usbd.c:430-434) right after an event is queued, here in ISR context from
 * dcd_event_handler() (usbd.c:1481-1482; the FSDEV ISR raises DCD_EVENT_BUS_RESET at dcd_stm32_fsdev.c:405).
 * Only latch a flag; delivery happens in main-loop context. */
void tud_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr)
{
    (void)rhport;
    (void)in_isr;
    if (eventid == (uint32_t)DCD_EVENT_BUS_RESET) {
        s_bus_reset = true;
    }
}

void usb_glue_deliver_bus_reset(void)
{
    if (s_app == NULL) {
        return; /* before the first usb_glue_task(): keep the flag */
    }
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const bool pending = s_bus_reset;
    s_bus_reset = false;
    __set_PRIMASK(primask);
    if (pending) {
        s_app_configured = false;
        dbg_usb_event(USBEV_BUS_RESET);
        app_usb_event(s_app, USBEV_BUS_RESET, s_now);
    }
}

/* ---------- TinyUSB device callbacks (task context, inside tud_task()) ---------- */

void tud_mount_cb(void)
{
    usb_glue_deliver_bus_reset();
    s_app_configured = true;
    dbg_usb_event(USBEV_CONFIGURED);
    app_usb_event(s_app, USBEV_CONFIGURED, s_now);
}

void tud_umount_cb(void)
{
    usb_glue_deliver_bus_reset();
    s_app_configured = false;
    dbg_usb_event(USBEV_UNCONFIGURED);
    app_usb_event(s_app, USBEV_UNCONFIGURED, s_now);
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en; /* remote wakeup is not offered (configuration bmAttributes bit 5 = 0) */
    usb_glue_deliver_bus_reset();
    dbg_usb_event(USBEV_SUSPEND);
    app_usb_event(s_app, USBEV_SUSPEND, s_now);
}

void tud_resume_cb(void)
{
    usb_glue_deliver_bus_reset();
    dbg_usb_event(USBEV_RESUME);
    app_usb_event(s_app, USBEV_RESUME, s_now);
}

/* Line coding and line state (DTR/RTS) callbacks are not implemented (weak defaults): they have no meaning
 * (spec 5.1, FW-23). */

/* ---------- API ---------- */

void usb_glue_init(void)
{
    /* Clean USB peripheral even after a start from the ROM loader (Q-2): pulse RCC_APBRSTR1.USBRST. */
    __HAL_RCC_USB_FORCE_RESET();
    __HAL_RCC_USB_RELEASE_RESET();
    __HAL_RCC_USB_CLK_ENABLE();
    s_bus_reset = false;
    /* PA11/PA12 stay in analog mode (board.c): USB DP/DM are an additional function on the C071, no AF needed
     * (same as lib/tinyusb/hw/bsp/stm32c0/family.c, which configures no USB pins). dcd_init() resets the core,
     * enables its interrupts and the DP pull-up (dcd_stm32_fsdev.c:181-202); tusb_init enables the NVIC line. */
    (void)tusb_init();
}

void usb_glue_task(app_t *app, ms_t now)
{
    s_app = app;
    s_now = now;
    tud_task();
    usb_glue_deliver_bus_reset();
    /* Safety net for one ordering: a bus reset whose ISR runs while tud_task() is already inside tud_mount_cb()
     * of the previous enumeration has its flag consumed there, BEFORE that (stale) CONFIGURED. The queued
     * DCD_EVENT_BUS_RESET is then processed by a later tud_task() (usbd_reset clears cfg_num) without any
     * callback, so the app would stay configured. Withdraw it as soon as TinyUSB no longer reports a mounted
     * configuration (tud_mounted() = cfg_num != 0; suspend keeps cfg_num). */
    if (s_app_configured && !tud_mounted()) {
        s_app_configured = false;
        dbg_usb_event(USBEV_BUS_RESET);
        app_usb_event(app, USBEV_BUS_RESET, now);
    }
    (void)tud_cdc_write_flush();
}

size_t usb_glue_tx_space(void *ctx)
{
    (void)ctx;
    return (size_t)tud_cdc_write_available();
}

void usb_glue_tx_write(void *ctx, const char *s, size_t n)
{
    (void)ctx;
    if (n == 0u) {
        return;
    }
    (void)tud_cdc_write(s, (uint32_t)n);
    (void)tud_cdc_write_flush();
}

bool usb_glue_rx_byte(char *c)
{
    uint8_t b;
    if (tud_cdc_read(&b, 1u) != 1u) {
        return false;
    }
    *c = (char)b;
    return true;
}

bool usb_glue_tx_drained(void)
{
    /* FIFO empty AND no IN transfer in flight (ruling: the reply must have left the device). The busy flag is
     * set by usbd_edpt_xfer() and cleared when tud_task() processes the transfer-complete event, i.e. after the
     * host ACKed the last packet (usbd.c:1644-1651). A ZLP after a full packet keeps it busy until sent
     * (cdc_device.c:518-523). */
    return tud_cdc_write_available() == CFG_TUD_CDC_TX_BUFSIZE && !usbd_edpt_busy(0, USB_EP_CDC_IN);
}

void usb_glue_service(void)
{
    s_now = HAL_GetTick(); /* callbacks fired here are stamped with the current tick, like usb_glue_task() */
    tud_task();
    (void)tud_cdc_write_flush();
}

void usb_glue_detach(void)
{
    (void)tud_disconnect(); /* DP pull-up off (dcd_disconnect): the host sees a detach */
}

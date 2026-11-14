/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
/* USB-DEBUG-CTRL r1 firmware entry: bare-metal superloop with a 1 ms SysTick tick (spec 6).
 * All behaviour lives in the pure core (core/app.h); this file only wires the target layer to it. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "core/app.h"
#include "stm32c0xx_hal.h"
#include "target/board.h"
#include "target/boot.h"
#include "target/clock.h"
#include "target/dbg_uart.h"
#include "target/power.h"
#include "target/usb_glue.h"

/* IWDG (spec 4.5): LSI 32 kHz / 32 = 1 kHz counter, reload 499 -> 500 counts = 500 ms nominal
 * (471..542 ms over the LSI range 34..29.5 kHz, DS14693 Rev 1 Table 43). Window disabled. */
#define IWDG_RELOAD 499u

static app_t app; /* large (SEQ program, reply buffer): static, never on the stack */
static char serial[25];
static IWDG_HandleTypeDef hiwdg;

void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* Defence in depth for the Stop path (spec 4.3 step 2): the outputs last written by board_write() are all off.
 * The app requests Stop only after the passthrough reached OFF or APP_STOP_WAIT_MS; never sleep with anything on. */
static bool outputs_all_off(const app_out_t *o)
{
    return o->relay_mask == 0u && !o->vbus_en && !o->data_en && !o->led_usb;
}

static void iwdg_start(void)
{
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = IWDG_RELOAD;
    hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
    /* The IWDG is running after the first key write inside HAL_IWDG_Init, whatever it returns. */
    (void)HAL_IWDG_Init(&hiwdg);
}

/* Refresh only from the main loop after the arbiter ran in this pass (spec 4.5).
 * Debug-only build option FW_TEST_STALL_MS (m-12, ruling C7): after a non-watchdog reset the loop stops refreshing
 * FW_TEST_STALL_MS after start; after a watchdog reset it never refreshes. A power-up therefore runs normally for
 * FW_TEST_STALL_MS, then the IWDG resets the board about every 0.5 s and the boot counter climbs. */
#ifdef FW_TEST_STALL_MS
static bool stall_now; /* set in main() when the last reset was the IWDG */
#endif

static void iwdg_refresh(ms_t now, ms_t t_start)
{
#ifdef FW_TEST_STALL_MS
    if (stall_now || (ms_t)(now - t_start) >= (ms_t)(FW_TEST_STALL_MS)) {
        return;
    }
#else
    (void)now;
    (void)t_start;
#endif
    (void)HAL_IWDG_Refresh(&hiwdg);
}

/* Close timing before a REBOOT / BOOTLOADER reset (spec 4.6, ruling Task 12, m-2). */
#define CLOSE_DRAIN_MAX_MS  100u /* bound for the OK to leave the device (FIFO empty, IN endpoint idle) */
#define CLOSE_LAST_PKT_MS   5u   /* margin after drained, for the host to take the last packet (m-2) */
#define CLOSE_DETACH_MS     50u  /* after the pull-up is off, so the host registers the detach */

/* Runs the USB stack and refreshes the IWDG for ms milliseconds, or (until_drained) until the CDC reply has left
 * the device, whichever comes first. */
static void close_wait(ms_t t_start, ms_t ms, bool service_usb, bool until_drained)
{
    const ms_t t0 = HAL_GetTick();
    for (;;) {
        const ms_t now = HAL_GetTick();
        if ((ms_t)(now - t0) >= ms || (until_drained && usb_glue_tx_drained())) {
            return;
        }
        if (service_usb) {
            usb_glue_service();
        }
        iwdg_refresh(now, t_start);
    }
}

static void close_usb_before_reset(ms_t t_start)
{
    /* Defence in depth: the app requests the reset only after safety_release and pt_is_off(), so its last
     * outputs are already off; write all-off once more (GPIOA with DATA_EN before GPIOB with VBUS_EN, board.c)
     * and stop calling app_tick/board_write until the reset. */
    const app_out_t off = {.relay_mask = 0u, .vbus_en = false, .data_en = false, .led_usb = false};
    board_write(&off);
    close_wait(t_start, CLOSE_DRAIN_MAX_MS, true, true);
    /* +1: with a 1 ms tick, "elapsed >= n + 1 ticks" guarantees at least n full milliseconds. */
    close_wait(t_start, CLOSE_LAST_PKT_MS + 1u, true, false);
    usb_glue_detach();
    close_wait(t_start, CLOSE_DETACH_MS + 1u, false, false);
}

int main(void)
{
    /* 1. Outputs to a defined OFF state before anything else (spec 4.4). */
    board_init_safe();

    /* 2. Reset cause (read once, then RMVF) and boot counter, before anything can cause a new reset. */
    const char *reset_token = boot_read_reset_token();
    const uint32_t boot_count = boot_count_increment();
#ifdef FW_TEST_STALL_MS
    stall_now = (strcmp(reset_token, "iwdg") == 0);
#endif

    /* 3. HAL time base, clocks (48 MHz + HSIUSB48/CRS), then the watchdog. */
    (void)HAL_Init();
    clock_init();
    dbg_init(); /* debug builds only (FW-32): USART1 needs the 48 MHz PCLK */
    iwdg_start();

    board_uid_hex(serial);
    dbg_boot(reset_token, boot_count, serial);
    /* USB CDC (TinyUSB): peripheral reset pulse, then enumeration may start. Events reach the app only from
     * usb_glue_task() in this loop. */
    usb_glue_init();
    dbg_printf("usb init done");

    const app_env_t env = {
        .tx_space = usb_glue_tx_space,
        .tx_write = usb_glue_tx_write,
        .ctx = NULL,
        .serial = serial,
        .fw_version = NULL, /* FW_VERSION from the build */
        .reset_token = reset_token,
        .boot_count = boot_count,
        .optr_err = !boot_optr_iwdg_stop_ok(), /* option-byte gate (spec 4.5): optr_err=1 -> never Stop */
    };
    app_in_t in;
    /* Not written to the pins before the first app_tick(); "everything on" so the Stop guard never trusts it. */
    app_out_t out = {.relay_mask = 0xFFu, .vbus_en = true, .data_en = true, .led_usb = true};
    board_read(&in);
    const ms_t t_start = HAL_GetTick();
    ms_t last = t_start;
    app_init(&app, &env, &in, last);

    for (;;) {
        /* USB events are stamped with the tick at delivery, not with `last`: after a stall in the loop (Stop path:
         * debug flush, SUSPRDY wait, clock restore) `last` is old and would eat into the RESUME hold
         * (APP_RESUME_CONFIRM_MS). HAL_GetTick() is monotonic and the next app_tick reads it again, so the times
         * the core sees stay non-decreasing. */
        usb_glue_task(&app, HAL_GetTick());
        dbg_poll();

        /* One app tick per pass when the 1 ms tick advanced, with the real time (a late pass skips samples,
         * the core timers stay exact). Outputs change only here. */
        const ms_t now = HAL_GetTick();
        if (now != last) {
            last = now;
            board_read(&in);
            app_tick(&app, now, &in, &out);
            board_write(&out);
            dbg_io(&in, &out);
            iwdg_refresh(now, t_start);
        }

        char c;
        while (app_can_accept_rx(&app) && usb_glue_rx_byte(&c)) {
            app_rx_byte(&app, c, last);
        }

        const app_request_t req = app_request(&app);
        switch (req) {
        case APP_REQ_REBOOT:
        case APP_REQ_BOOTLOADER:
            /* Spec 4.6: the app made every output off (safety_release, passthrough OFF; board_write above wrote
             * it) and handed the OK to the CDC FIFO. Drain, detach, then reset: REBOOT without a record,
             * BOOTLOADER with the record that Reset_Handler turns into the ROM jump. */
            dbg_printf("%s: closing USB, reset", req == APP_REQ_BOOTLOADER ? "bootloader" : "reboot");
            close_usb_before_reset(t_start);
            dbg_flush(DBG_FLUSH_MS);
            if (req == APP_REQ_BOOTLOADER) {
                boot_request_bootloader_and_reset();
            }
            NVIC_SystemReset();
            break;
        case APP_REQ_STOP:
            /* Spec 4.3 steps 3-6 (Task 11). Entered once per request; power re-checks app_request() and the USB
             * wake/reset flags with interrupts masked right before Stop. false = SUSPRDY timeout, clock restore
             * failure or IWDG_STOP option byte not 0: lp_err, stay in Run with everything off (spec 4.3 fallback).
             * A spurious wake on an idle bus gives a new ISTR.SUSP -> SUSPEND -> the next STOP request. */
            dbg_printf("enter stop");
            dbg_flush(DBG_FLUSH_MS);
            if (!outputs_all_off(&out) || !power_enter_stop_until_usb_wake(&app)) {
                dbg_printf("lp_err: %s", outputs_all_off(&out) ? "stop path failed" : "outputs not off");
                app_set_lp_err(&app);
            } else {
                dbg_printf("wake");
            }
            app_stop_done(&app);
            break;
        case APP_RUN:
        default:
            break;
        }
    }
}

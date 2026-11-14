/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef TARGET_POWER_H
#define TARGET_POWER_H
#include <stdbool.h>

#include "core/app.h"

/* USB suspend low-power path, spec 4.3 steps 3-6 (FW-06, FW-08, FW-09).
 *
 * Call from the main loop only, when app_request(app) == APP_REQ_STOP and every output is off. It
 *   1. confirms the USB suspend: SUSPEN = 1, poll SUSPRDY for at most POWER_SUSPRDY_TIMEOUT_MS;
 *   2. masks interrupts (PRIMASK) and re-checks that Stop is still wanted: app_request(app) == APP_REQ_STOP, no
 *      USB_ISTR WKUP / RESET, no TinyUSB event queued (tud_task_event_ready()), SUSPRDY still 1;
 *   3. unmasks the USB wake-up on EXTI line 36, clears every other NVIC pending bit, stops SysTick, enters Stop 0
 *      with WFI (wakes on the pending USB interrupt although PRIMASK = 1);
 *   4. on wake: restores the clocks (clock_restore_after_stop), restarts SysTick, then unmasks interrupts, so the
 *      USB ISR handles the resume / reset only with the 48 MHz system clock and HSIUSB48 back.
 *
 * Returns
 *   true  - woke from Stop with the clocks restored, or Stop was skipped because a USB event is already pending or
 *           the app no longer requests Stop (nothing changed);
 *   false - SUSPRDY did not set within the timeout (Stop not entered) or the clock restore after Stop failed.
 *           Caller: app_set_lp_err(), stay in Run with outputs off (spec 4.3 fallback).
 * Never enters Stop when the app reports optr_err (app_request() is then never APP_REQ_STOP, spec 4.5). */
#define POWER_SUSPRDY_TIMEOUT_MS 5u

bool power_enter_stop_until_usb_wake(const app_t *app);

#endif

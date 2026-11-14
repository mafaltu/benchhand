/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
/* USB suspend -> Stop 0 -> USB wake (spec 4.3 steps 3-6, FW-06, FW-08, FW-09). See power.h for the contract.
 *
 * References (RM0490 Rev 6):
 *   - USB_CNTR SUSPEN / SUSPRDY, pp. 977-978: SUSPRDY "must be polled ... before any STOP mode entry"; both bits are
 *     cleared by hardware together with ISTR.WKUP (p. 978, ISTR.WKUP p. 981).
 *   - Suspend/resume procedure, 29.5.7 pp. 972-973.
 *   - HSIUSB48 is disabled in Stop, 6.2.3 p. 125 -> clock_restore_after_stop() re-enables it and waits for RDY.
 *   - EXTI line 36 = USB wake-up, direct line (Table 60, p. 266); EXTI_IMR2.IM36 (14.5.14, p. 276) resets to 0 and
 *     TinyUSB never sets it (fsdev_stm32.h:176-182). A direct line has no EXTI pending bit; the
 *     USB peripheral interrupt itself wakes the CPU (14.3.2, p. 265), so USB_DRD_FS_IRQn must stay NVIC-enabled.
 *   - Low-power entry (5.3.2, Table 28, p. 99): WFI with SLEEPDEEP = 1 and LPMS = 000 enters Stop 0; the entry is
 *     aborted while an interrupt is pending, so a USB interrupt that becomes pending after the checks below
 *     simply makes WFI fall through.
 *   - Armv6-M: WFI wakes on a pending enabled interrupt even with PRIMASK = 1; the handler runs only after
 *     PRIMASK is cleared, i.e. after the clocks are restored. */
#include "target/power.h"

#include <stdbool.h>
#include <stdint.h>

#include "stm32c0xx.h"
#include "stm32c0xx_hal.h"
#include "target/boot.h"
#include "target/clock.h"
#include "target/dbg_uart.h"
#include "tusb.h"

/* A wake-up / reset already happened or TinyUSB queued an event the main loop has not processed yet. Either one
 * means "do not (stay) suspend(ed)": the main loop must run tud_task() and the app first. */
static bool usb_event_pending(void)
{
    return (USB_DRD_FS->ISTR & (USB_ISTR_WKUP | USB_ISTR_RESET)) != 0u || tud_task_event_ready();
}

/* Everything that must still hold, checked with interrupts masked right before the suspend / Stop step. */
static bool stop_still_wanted(const app_t *app)
{
    return app_request(app) == APP_REQ_STOP && !usb_event_pending();
}

bool power_enter_stop_until_usb_wake(const app_t *app)
{
    /* Spec 4.5: Stop only with the IWDG frozen in Stop (FLASH_OPTR.IWDG_STOP = 0). The app never requests Stop
     * with optr_err = 1; this is the target-side guard (failure -> lp_err, stay in Run). */
    if (!boot_optr_iwdg_stop_ok()) {
        dbg_printf("stop: IWDG_STOP option byte not 0");
        return false;
    }

    /* ---- Step 3: request the USB suspend state (RM0490 p. 978). TinyUSB already set SUSPEN in its SUSP ISR
     * (dcd_stm32_fsdev.c:417-427). Set it again only while nothing woke the bus: SUSPEN written after a wake
     * would gate an active device. Masked so the USB ISR cannot slip in between the check and the write. */
    __disable_irq();
    if (!stop_still_wanted(app)) {
        __enable_irq();
        dbg_printf("stop: skipped (usb event pending or request withdrawn)");
        return true; /* resume / reset / event pending: nothing changed, the main loop handles it */
    }
    USB_DRD_FS->CNTR |= USB_CNTR_SUSPEN;
    __enable_irq();

    /* Poll SUSPRDY for at most POWER_SUSPRDY_TIMEOUT_MS (gives up once 5 HAL ticks have elapsed; with the 1 ms tick
     * that is 4..5 ms of real time, never more than 5 ms). Interrupts stay enabled:
     * the HAL tick advances and a wake-up during the wait is handled by the USB ISR (which queues an event).
     * Should the tick ever stop, the IWDG (500 ms, refreshed only by the main loop) resets the board. */
    const uint32_t t0 = HAL_GetTick();
    bool ready = false;
    for (;;) {
        ready = (USB_DRD_FS->CNTR & USB_CNTR_SUSPRDY) != 0u;
        if (ready || usb_event_pending()) {
            break;
        }
        if ((uint32_t)(HAL_GetTick() - t0) >= POWER_SUSPRDY_TIMEOUT_MS) {
            break;
        }
    }
    if (!ready) {
        /* Woken during the wait: not a failure, the main loop processes the resume / reset.
         * Otherwise SUSPRDY timeout: stay in Run with outputs off, lp_err (spec 4.3 fallback). */
        const bool woken = usb_event_pending();
        dbg_printf("stop: %s", woken ? "usb event during SUSPRDY wait" : "SUSPRDY timeout");
        return woken;
    }

    /* ---- Steps 3-5 with interrupts masked (PRIMASK = 1) from here until the clocks are restored: the USB ISR
     * cannot run on the post-Stop HSISYS clock without HSIUSB48. */
    __disable_irq();
    if (!stop_still_wanted(app) || (USB_DRD_FS->CNTR & USB_CNTR_SUSPRDY) == 0u) {
        __enable_irq();
        dbg_printf("stop: skipped late (usb event, request withdrawn or SUSPRDY cleared)");
        return true; /* host resumed / reset meanwhile, or the app cancelled the request */
    }

    /* Wake source: USB wake-up on EXTI line 36 (direct, IMR2.IM36, RM0490 14.5.14 p. 276, Table 60 p. 266). */
    EXTI->IMR2 |= EXTI_IMR2_IM36;

    /* Step 4: clear the other wake sources. No configurable EXTI line is used (all inputs are polled); clear any
     * stale pending bit anyway (write 1 to clear). Then every NVIC pending bit except the USB one: a still
     * pending USB interrupt must abort the Stop entry (Table 28, p. 99). */
    EXTI->RPR1 = EXTI->RPR1;
    EXTI->FPR1 = EXTI->FPR1;
    EXTI->RPR2 = EXTI->RPR2;
    EXTI->FPR2 = EXTI->FPR2;
    NVIC->ICPR[0] = ~(1UL << (uint32_t)USB_DRD_FS_IRQn);

    /* Stop SysTick (counter and interrupt) and drop a pending SysTick exception, which would end WFI at once. */
    const uint32_t systick_ctrl = SysTick->CTRL & (SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk |
                                                   SysTick_CTRL_ENABLE_Msk);
    SysTick->CTRL = systick_ctrl & ~(SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk);
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* Step 5: Stop 0 = PWR_CR1.LPMS 000 + SLEEPDEEP + WFI (Table 28, p. 99). Register for register the body of
     * HAL_PWR_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFI) (stm32c0xx_hal_pwr.c:300-326), written out
     * here because that vendor file does not build warning-free (-Wunused-parameter 'Regulator'). PWR bus clock:
     * clock.c (APBENR1.PWREN). WFI returns after the wake-up, or at once when an interrupt is pending; still
     * PRIMASK = 1, so the USB handler does not run yet. The DSB makes sure the SCR / CR1 writes are done. */
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_LPMS) | PWR_LOWPOWERMODE_STOP0;
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;

    /* Step 6: 48 MHz SYSCLK, HSIUSB48 on and ready, CRS on SOF (keeps the trim), SysTick re-armed by
     * HAL_InitTick inside; restore the SysTick control bits explicitly in case the restore failed early. */
    const bool clk_ok = clock_restore_after_stop();
    SysTick->CTRL = systick_ctrl;
    EXTI->IMR2 &= ~EXTI_IMR2_IM36; /* Run: back to the reset state TinyUSB expects */
    __enable_irq();                /* the USB ISR now handles WKUP (resume) or RESET */
    dbg_printf("stop: woke from Stop 0, clock restore %s", clk_ok ? "ok" : "FAILED");

    return clk_ok;
}

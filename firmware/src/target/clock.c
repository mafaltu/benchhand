/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "target/clock.h"

#include <stdint.h>

#include "stm32c0xx.h"
#include "stm32c0xx_hal.h"

/* CRS on USB SOF (RM0490 Rev 6 7.4.6): RELOAD = fTARGET / fSYNC - 1 = 48 MHz / 1 kHz - 1 = 47999.
 * FELIM = (fTARGET / fSYNC) * STEP / 2, rounded up = 48000 * 0.11 % / 2 = 26.4 -> 27, with the typical HSIUSB48
 * trimming step 0.11 % (DS14693 Rev 1 Table 42). RELOAD > 128 * FELIM holds (47999 > 3456). */
#define CRS_RELOAD       47999u
#define CRS_FELIM        27u
#define CRS_SYNCSRC_SOF  2u  /* SYNCSRC = 10: crs_sync_in_2 = USB SOF (RM0490 7.3, Table "CRS internal input/output signals") */
#define CRS_TRIM_DEFAULT 64u /* CRS_CR TRIM reset value, middle of the 0..127 range */

/* Bounded busy-wait for a register condition. Each iteration takes at least ~4 CPU cycles; 20000 iterations are
 * >= 1.6 ms at 48 MHz, far above the slowest condition waited for here (HSIUSB48 start-up <= 6 us, DS14693
 * Table 42; HSI48 is already running as the system clock). */
#define WAIT_LOOPS 20000u

static bool wait_set(volatile const uint32_t *reg, uint32_t mask, uint32_t want)
{
    for (uint32_t i = 0; i < WAIT_LOOPS; i++) {
        if ((*reg & mask) == want) {
            return true;
        }
    }
    return (*reg & mask) == want;
}

static bool clock_config(bool keep_trim)
{
    bool ok = true;

    /* Bus clocks used below: PWR, CRS (APB), SYSCFG not needed. */
    RCC->APBENR1 |= RCC_APBENR1_PWREN | RCC_APBENR1_CRSEN;
    (void)RCC->APBENR1;

    /* 1. HSI48 on (it is the reset / Stop-exit system clock source, so this only confirms it). */
    RCC->CR |= RCC_CR_HSION;
    ok &= wait_set(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY);

    /* 2. Flash latency before raising HCLK (RM0490 4.3.2): 1 WS for HCLK <= 48 MHz; prefetch and I-cache on.
     *    LATENCY must read back before the clock rises. */
    FLASH->ACR = (FLASH->ACR & ~(FLASH_ACR_LATENCY | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN)) | FLASH_ACR_LATENCY_0 |
                 FLASH_ACR_PRFTEN | FLASH_ACR_ICEN;
    ok &= wait_set(&FLASH->ACR, FLASH_ACR_LATENCY, FLASH_ACR_LATENCY_0);

    /* 3. System clock switch = HSISYS (SW = 000), AHB / APB prescalers 1, MCO / MCO2 off. Wait for SWS = 000. */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE | RCC_CFGR_MCOSEL | RCC_CFGR_MCOPRE |
                               RCC_CFGR_MCO2SEL | RCC_CFGR_MCO2PRE));
    ok &= wait_set(&RCC->CFGR, RCC_CFGR_SWS, 0u);

    /* 4. HSISYS = HSI48 / 1 and SYSCLK divider 1 -> 48 MHz; HSIUSB48 on. HSIKERDIV / HSIKERON are not used
     *    (no peripheral on HSIKER) and keep their values. */
    RCC->CR = (RCC->CR & ~(RCC_CR_HSIDIV | RCC_CR_SYSDIV)) | RCC_CR_HSIUSB48ON;
    ok &= wait_set(&RCC->CR, RCC_CR_HSIUSB48RDY, RCC_CR_HSIUSB48RDY);

    /* 5. USB kernel clock = HSIUSB48 (CCIPR2.USBSEL = 0, RM0490 6.4.21). */
    RCC->CCIPR2 &= ~RCC_CCIPR2_USBSEL;

    /* 6. CRS: stop the counter (CFGR is write-protected while CEN = 1), configure SOF sync, then enable
     *    automatic trimming and the counter. Interrupts off; the trim is kept across Stop when asked. */
    const uint32_t trim = keep_trim ? (CRS->CR & CRS_CR_TRIM) : (CRS_TRIM_DEFAULT << CRS_CR_TRIM_Pos);
    CRS->CR = trim;
    CRS->CFGR = (CRS_SYNCSRC_SOF << CRS_CFGR_SYNCSRC_Pos) | (CRS_FELIM << CRS_CFGR_FELIM_Pos) |
                (CRS_RELOAD << CRS_CFGR_RELOAD_Pos); /* SYNCPOL = 0 rising, SYNCDIV = 0 (/1) */
    CRS->ICR = CRS_ICR_SYNCOKC | CRS_ICR_SYNCWARNC | CRS_ICR_ERRC | CRS_ICR_ESYNCC;
    CRS->CR = trim | CRS_CR_AUTOTRIMEN | CRS_CR_CEN;

    /* 7. SystemCoreClock and the 1 ms SysTick follow the new HCLK. */
    SystemCoreClockUpdate();
    if (HAL_InitTick(TICK_INT_PRIORITY) != HAL_OK) {
        ok = false;
    }
    return ok;
}

void clock_init(void)
{
    /* A failure here leaves the MCU running (on HSISYS) without a ready USB clock: USB will not enumerate, so
     * CONFIGURED never arrives and the relays stay inhibited (safe). Nothing else to do at this point. */
    (void)clock_config(false);
}

bool clock_restore_after_stop(void)
{
    return clock_config(true);
}

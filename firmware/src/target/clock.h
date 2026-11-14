/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef TARGET_CLOCK_H
#define TARGET_CLOCK_H
#include <stdbool.h>

/* Clock tree (spec 2, 4.3 step 6; RM0490 Rev 6 6.4.1, 6.4.3, 6.4.21, 7.7, 4.3.2):
 *   SYSCLK = HSISYS = HSI48 / HSIDIV(1) = 48 MHz, SYSDIV 1, AHB 1, APB 1; flash LATENCY 1 WS (HCLK <= 48 MHz).
 *   HSIUSB48 on; USB kernel clock = HSIUSB48 (CCIPR2.USBSEL = 0); CRS trims HSIUSB48 on USB SOF
 *   (SYNCSRC = 10, RELOAD = 47999, FELIM = 34, AUTOTRIMEN). Every field used is written; no reset value is assumed.
 * Also updates SystemCoreClock and re-arms the 1 ms SysTick (HAL_InitTick). */
void clock_init(void);

/* After a Stop exit (SYSCLK falls back to HSISYS, HSIUSB48 is off): the same configuration, keeping the CRS trim
 * learned before Stop, plus a bounded wait for HSIUSB48RDY. False if HSIRDY / LATENCY / SWS / HSIUSB48RDY did not
 * settle within the bound (caller: stay in Run, lp_err, spec 4.3 fallback). */
bool clock_restore_after_stop(void);

#endif

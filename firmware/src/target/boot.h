/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef TARGET_BOOT_H
#define TARGET_BOOT_H
#include <stdbool.h>
#include <stdint.h>

/* Boot bookkeeping (spec 4.4, 4.5, 4.6). */

/* No-init RAM record {magic, ~magic, count}: count + 1 when the record is valid and the reset was not a
 * power-on/brown-out reset, else a fresh record with count = 1. Returns the new count. Call once, after
 * boot_read_reset_token(). */
uint32_t boot_count_increment(void);

/* Reads RCC_CSR2 once (cached), returns boot_reset_token() of it and clears the flags (RMVF).
 * Later calls return the same token. */
const char *boot_read_reset_token(void);

/* FLASH_OPTR.IWDG_STOP == 0 (IWDG frozen in Stop, spec 4.5). The firmware never writes option bytes. */
bool boot_optr_iwdg_stop_ok(void);

/* Called by Reset_Handler before SystemInit and before .data/.bss are initialised: registers and .noinit only,
 * no stack-heavy code (spec 4.6). Reads RCC_CSR2 (not cleared here; main() reads it again for reset=),
 * j = boot_record_take(): the BOOTLOADER record is checked (SFTRSTF set, PWR/IWDG/WWDG/LPWR/OBL clear, PINRSTF
 * ignored, FW-26) and cleared in every case. j -> jump to the ROM system memory loader (USB DFU), no return. */
void boot_early_check(void);

/* BOOTLOADER command (spec 4.6), after the outputs are off and USB is detached: interrupts off, write
 * {BOOT_MAGIC, ~BOOT_MAGIC, ++counter} to the .noinit record, NVIC_SystemReset(). Does not return. */
void boot_request_bootloader_and_reset(void) __attribute__((noreturn));

#endif

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_BOOTREC_H
#define CORE_BOOTREC_H
#include <stdbool.h>
#include <stdint.h>

/* Reset-cause decoding (spec 4.4, FW-26). Pure: takes the RCC_CSR2 value, no hardware access.
 * RCC_CSR2 reset flag bits, RM0490 Rev 6 6.4.23 p. 161. */
#define CSR2_LPWRRSTF (1u << 31)
#define CSR2_WWDGRSTF (1u << 30)
#define CSR2_IWDGRSTF (1u << 29)
#define CSR2_SFTRSTF  (1u << 28)
#define CSR2_PWRRSTF  (1u << 27)
#define CSR2_PINRSTF  (1u << 26) /* set on every system reset, not only the NRST pin */
#define CSR2_OBLRSTF  (1u << 25)

/* One token: the first set of por, obl, iwdg, wwdg, lpwr, sw (in that order); otherwise "pin". */
const char *boot_reset_token(uint32_t csr2);

/* The one record-validity rule for every no-init RAM record (boot counter, BOOTLOADER request; ruling C3):
 * magic == expected and magic_inv == ~expected. */
bool bootrec_magic_valid(uint32_t magic, uint32_t magic_inv, uint32_t expected);

/* ---------- BOOTLOADER request record (spec 4.6, FW-18, FW-26) ----------
 * Lives in .noinit; written by the BOOTLOADER command right before the software reset, checked and cleared by
 * boot_early_check() in Reset_Handler. All functions below are pure, use no static data and run before
 * .data/.bss are initialised. */
#define BOOT_MAGIC 0xB007D0F0u
typedef struct {
    uint32_t magic, magic_inv, counter;
} boot_record_t;

/* Reset flags that forbid the jump (spec 4.6): anything but a plain software reset. PINRSTF is not among them:
 * it is set on every system reset (RM0490 Rev 6 RCC_CSR2 bit 26, p. 161; FW-26). */
#define CSR2_JUMP_FORBID (CSR2_PWRRSTF | CSR2_IWDGRSTF | CSR2_WWDGRSTF | CSR2_LPWRRSTF | CSR2_OBLRSTF)

/* true iff (csr2 & SFTRSTF) && !(csr2 & CSR2_JUMP_FORBID) && the record's magic pair is valid. The reset
 * flags are checked first: the record is not read at all after a power-on reset (SRAM parity, RM0490 2.3). */
bool boot_should_jump(const boot_record_t *rec, uint32_t csr2);

/* BOOTLOADER command: rec = {BOOT_MAGIC, ~BOOT_MAGIC, counter + 1}. */
void boot_record_arm(boot_record_t *rec);

/* Reset_Handler: decision = boot_should_jump(rec, csr2), then the record is cleared in every case
 * (magic = magic_inv = 0). After a power-on reset (PWRRSTF) the record is only written, never read, and the
 * counter restarts at 0; after any other reset the counter (BOOTLOADER requests since power-on) is kept.
 * Returns the decision. */
bool boot_record_take(boot_record_t *rec, uint32_t csr2);

#endif

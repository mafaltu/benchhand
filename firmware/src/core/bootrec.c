/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/bootrec.h"

#include <stddef.h>

/* Priority order of spec 4.4: por, obl, iwdg, wwdg, lpwr, sw; PINRSTF alone (or nothing) = "pin". */
static const struct {
    uint32_t flag;
    const char *token;
} reset_order[] = {
    {CSR2_PWRRSTF, "por"},   {CSR2_OBLRSTF, "obl"},   {CSR2_IWDGRSTF, "iwdg"},
    {CSR2_WWDGRSTF, "wwdg"}, {CSR2_LPWRRSTF, "lpwr"}, {CSR2_SFTRSTF, "sw"},
};

const char *boot_reset_token(uint32_t csr2)
{
    for (size_t i = 0; i < sizeof reset_order / sizeof reset_order[0]; i++) {
        if ((csr2 & reset_order[i].flag) != 0u) {
            return reset_order[i].token;
        }
    }
    return "pin";
}

bool bootrec_magic_valid(uint32_t magic, uint32_t magic_inv, uint32_t expected)
{
    return magic == expected && magic_inv == ~expected;
}

bool boot_should_jump(const boot_record_t *rec, uint32_t csr2)
{
    /* Reset flags first (short-circuit): after a power-on reset the record is not read at all. */
    if ((csr2 & CSR2_SFTRSTF) == 0u || (csr2 & CSR2_JUMP_FORBID) != 0u) {
        return false;
    }
    return bootrec_magic_valid(rec->magic, rec->magic_inv, BOOT_MAGIC);
}

void boot_record_arm(boot_record_t *rec)
{
    rec->magic = BOOT_MAGIC;
    rec->magic_inv = ~BOOT_MAGIC;
    rec->counter++;
}

bool boot_record_take(boot_record_t *rec, uint32_t csr2)
{
    if ((csr2 & CSR2_PWRRSTF) != 0u) {
        /* Fresh SRAM: write every word before anything reads it (RM0490 Rev 6 2.3, RAM_PARITY_CHECK). */
        rec->magic = 0u;
        rec->magic_inv = 0u;
        rec->counter = 0u;
        return false;
    }
    const bool jump = boot_should_jump(rec, csr2);
    rec->magic = 0u; /* cleared in every case (spec 4.6): a stale or corrupt record is never used twice */
    rec->magic_inv = 0u;
    return jump;
}

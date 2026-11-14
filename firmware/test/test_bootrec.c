/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "minitest.h"
#include "core/bootrec.h"

/* m-7: one prioritized reset token (spec 4.4, FW-26). PINRSTF accompanies every system reset
 * (RM0490 Rev 6 p. 161), so the tests add it to every other cause. */
TEST(test_reset_token_table)
{
    /* each single flag alone */
    CHECK_EQ_STR(boot_reset_token(CSR2_PWRRSTF), "por");
    CHECK_EQ_STR(boot_reset_token(CSR2_OBLRSTF), "obl");
    CHECK_EQ_STR(boot_reset_token(CSR2_IWDGRSTF), "iwdg");
    CHECK_EQ_STR(boot_reset_token(CSR2_WWDGRSTF), "wwdg");
    CHECK_EQ_STR(boot_reset_token(CSR2_LPWRRSTF), "lpwr");
    CHECK_EQ_STR(boot_reset_token(CSR2_SFTRSTF), "sw");
    CHECK_EQ_STR(boot_reset_token(CSR2_PINRSTF), "pin");

    /* with PINRSTF added */
    CHECK_EQ_STR(boot_reset_token(CSR2_PINRSTF | CSR2_PWRRSTF), "por");
    CHECK_EQ_STR(boot_reset_token(CSR2_PINRSTF | CSR2_SFTRSTF), "sw");
    CHECK_EQ_STR(boot_reset_token(CSR2_PINRSTF | CSR2_IWDGRSTF), "iwdg");
    CHECK_EQ_STR(boot_reset_token(CSR2_PINRSTF | CSR2_OBLRSTF), "obl");
    CHECK_EQ_STR(boot_reset_token(CSR2_PINRSTF | CSR2_WWDGRSTF | CSR2_SFTRSTF), "wwdg");
    CHECK_EQ_STR(boot_reset_token(CSR2_PINRSTF | CSR2_LPWRRSTF), "lpwr");

    /* nothing set */
    CHECK_EQ_STR(boot_reset_token(0u), "pin");
}

TEST(test_reset_token_priority)
{
    /* por > obl > iwdg > wwdg > lpwr > sw (spec 4.4) */
    const uint32_t all = CSR2_LPWRRSTF | CSR2_WWDGRSTF | CSR2_IWDGRSTF | CSR2_SFTRSTF | CSR2_PWRRSTF |
                         CSR2_PINRSTF | CSR2_OBLRSTF;
    CHECK_EQ_STR(boot_reset_token(all), "por");
    CHECK_EQ_STR(boot_reset_token(all & ~CSR2_PWRRSTF), "obl");
    CHECK_EQ_STR(boot_reset_token(all & ~(CSR2_PWRRSTF | CSR2_OBLRSTF)), "iwdg");
    CHECK_EQ_STR(boot_reset_token(CSR2_WWDGRSTF | CSR2_LPWRRSTF | CSR2_SFTRSTF), "wwdg");
    CHECK_EQ_STR(boot_reset_token(CSR2_LPWRRSTF | CSR2_SFTRSTF), "lpwr");
    /* bits outside the flag set are ignored (RMVF, LSIRDY, LSION) */
    CHECK_EQ_STR(boot_reset_token((1u << 23) | 0x3u), "pin");
}

TEST(test_csr2_bit_values)
{
    /* RM0490 Rev 6 6.4.23 p. 161 */
    CHECK_EQ_INT(CSR2_LPWRRSTF, 0x80000000u);
    CHECK_EQ_INT(CSR2_WWDGRSTF, 0x40000000u);
    CHECK_EQ_INT(CSR2_IWDGRSTF, 0x20000000u);
    CHECK_EQ_INT(CSR2_SFTRSTF, 0x10000000u);
    CHECK_EQ_INT(CSR2_PWRRSTF, 0x08000000u);
    CHECK_EQ_INT(CSR2_PINRSTF, 0x04000000u);
    CHECK_EQ_INT(CSR2_OBLRSTF, 0x02000000u);
}

/* ---------- Task 12: BOOTLOADER record and jump decision (spec 4.6, FW-26, plan review C-1) ---------- */

static const boot_record_t VALID = {BOOT_MAGIC, ~BOOT_MAGIC, 1u};
/* Every reset flag that forbids the jump (spec 4.6): PWR, IWDG, WWDG, LPWR, OBL. */
static const uint32_t FORBID[] = {CSR2_PWRRSTF, CSR2_IWDGRSTF, CSR2_WWDGRSTF, CSR2_LPWRRSTF, CSR2_OBLRSTF};
#define N_FORBID (sizeof FORBID / sizeof FORBID[0])

/* Fixed-seed LCG (Numerical Recipes constants): reproducible "random RAM". */
static uint32_t lcg_state;
static uint32_t lcg_next(void)
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

TEST(test_boot_magic_value)
{
    CHECK_EQ_INT(BOOT_MAGIC, 0xB007D0F0u);
    CHECK(bootrec_magic_valid(BOOT_MAGIC, ~BOOT_MAGIC, BOOT_MAGIC));
    CHECK(!bootrec_magic_valid(BOOT_MAGIC, BOOT_MAGIC, BOOT_MAGIC));
    CHECK(!bootrec_magic_valid(~BOOT_MAGIC, BOOT_MAGIC, BOOT_MAGIC));
    CHECK(!bootrec_magic_valid(0u, 0u, BOOT_MAGIC));
    CHECK(!bootrec_magic_valid(0xFFFFFFFFu, 0u, BOOT_MAGIC));
}

TEST(test_jump_por_random_ram)
{
    /* Power-on: SRAM is random, PWRRSTF (+ PINRSTF) set -> never DFU, whatever the RAM holds. */
    const uint32_t por = CSR2_PWRRSTF | CSR2_PINRSTF;
    lcg_state = 12345u;
    int jumps = 0;
    for (int i = 0; i < 10000; i++) {
        boot_record_t r = {lcg_next(), lcg_next(), lcg_next()};
        if (i % 7 == 0) { /* also records whose magic pair is consistent */
            r.magic = BOOT_MAGIC;
            r.magic_inv = ~BOOT_MAGIC;
        }
        if (boot_should_jump(&r, por)) {
            jumps++;
        }
    }
    CHECK_EQ_INT(jumps, 0);
    CHECK(!boot_should_jump(&VALID, por));
    CHECK(!boot_should_jump(&VALID, CSR2_PWRRSTF));
}

TEST(test_jump_pin_only)
{
    CHECK(!boot_should_jump(&VALID, CSR2_PINRSTF)); /* NRST pin */
    CHECK(!boot_should_jump(&VALID, 0u));
}

TEST(test_jump_sw_reset)
{
    CHECK(boot_should_jump(&VALID, CSR2_SFTRSTF | CSR2_PINRSTF)); /* real software reset (FW-26) */
    CHECK(boot_should_jump(&VALID, CSR2_SFTRSTF));
    /* bits outside the reset flags do not matter (RMVF, LSIRDY, LSION) */
    CHECK(boot_should_jump(&VALID, CSR2_SFTRSTF | CSR2_PINRSTF | 0x3u));
}

TEST(test_jump_sw_plus_other)
{
    for (unsigned i = 0; i < N_FORBID; i++) {
        CHECK(!boot_should_jump(&VALID, CSR2_SFTRSTF | CSR2_PINRSTF | FORBID[i]));
        CHECK(!boot_should_jump(&VALID, CSR2_SFTRSTF | FORBID[i]));
    }
}

TEST(test_jump_bad_record)
{
    const uint32_t sw = CSR2_SFTRSTF | CSR2_PINRSTF;
    boot_record_t r = VALID;
    r.magic_inv ^= 1u;
    CHECK(!boot_should_jump(&r, sw));
    r = VALID;
    r.magic ^= 0x80000000u;
    CHECK(!boot_should_jump(&r, sw));
    r = VALID;
    r.magic = ~BOOT_MAGIC;
    r.magic_inv = BOOT_MAGIC;
    CHECK(!boot_should_jump(&r, sw));
    const boot_record_t zero = {0u, 0u, 0u};
    CHECK(!boot_should_jump(&zero, sw));
    /* the counter value is not part of the validity check */
    r = VALID;
    r.counter = 0xDEADBEEFu;
    CHECK(boot_should_jump(&r, sw));
}

TEST(test_record_arm)
{
    /* BOOTLOADER command: {BOOT_MAGIC, ~BOOT_MAGIC, ++counter} */
    boot_record_t r = {0u, 0u, 0u};
    boot_record_arm(&r);
    CHECK_EQ_INT(r.magic, BOOT_MAGIC);
    CHECK_EQ_INT(r.magic_inv, ~BOOT_MAGIC);
    CHECK_EQ_INT(r.counter, 1u);
    boot_record_arm(&r);
    CHECK_EQ_INT(r.counter, 2u);
}

TEST(test_record_take_clears_every_case)
{
    /* Every reset cause (each forbidden flag, pin only, sw): the record is cleared after the decision. */
    const uint32_t causes[] = {CSR2_PWRRSTF | CSR2_PINRSTF, CSR2_IWDGRSTF | CSR2_PINRSTF,
                               CSR2_WWDGRSTF | CSR2_PINRSTF, CSR2_LPWRRSTF | CSR2_PINRSTF,
                               CSR2_OBLRSTF | CSR2_PINRSTF, CSR2_PINRSTF, CSR2_SFTRSTF | CSR2_PINRSTF,
                               CSR2_SFTRSTF | CSR2_IWDGRSTF | CSR2_PINRSTF};
    const int want[] = {0, 0, 0, 0, 0, 0, 1, 0};
    for (unsigned i = 0; i < sizeof causes / sizeof causes[0]; i++) {
        boot_record_t r = {BOOT_MAGIC, ~BOOT_MAGIC, 5u};
        CHECK_EQ_INT(boot_record_take(&r, causes[i]), want[i]);
        CHECK(!bootrec_magic_valid(r.magic, r.magic_inv, BOOT_MAGIC));
        CHECK(!boot_should_jump(&r, CSR2_SFTRSTF | CSR2_PINRSTF)); /* a second reset never jumps again */
    }
    /* a corrupt record is cleared and ignored */
    boot_record_t c = {BOOT_MAGIC, 0x12345678u, 9u};
    CHECK(!boot_record_take(&c, CSR2_SFTRSTF | CSR2_PINRSTF));
    CHECK_EQ_INT(c.magic, 0u);
    CHECK_EQ_INT(c.magic_inv, 0u);
}

TEST(test_record_take_counter)
{
    /* Power-on: the whole record is written (fresh SRAM, parity, RM0490 Rev 6 2.3), counter restarts at 0. */
    boot_record_t r = {0xA5A5A5A5u, 0x5A5A5A5Au, 0x77777777u};
    CHECK(!boot_record_take(&r, CSR2_PWRRSTF | CSR2_PINRSTF));
    CHECK_EQ_INT(r.magic, 0u);
    CHECK_EQ_INT(r.magic_inv, 0u);
    CHECK_EQ_INT(r.counter, 0u);
    /* Other resets keep the request counter (BOOTLOADER requests since power-on). */
    boot_record_arm(&r);
    boot_record_arm(&r);
    CHECK(boot_record_take(&r, CSR2_SFTRSTF | CSR2_PINRSTF));
    CHECK_EQ_INT(r.counter, 2u);
    CHECK(!boot_record_take(&r, CSR2_PINRSTF));
    CHECK_EQ_INT(r.counter, 2u);
}

TEST(test_record_sequence)
{
    /* BOOTLOADER -> reset (jump) -> ROM -> reset back to the app: no second jump. */
    boot_record_t r = {0u, 0u, 0u};
    (void)boot_record_take(&r, CSR2_PWRRSTF | CSR2_PINRSTF);
    boot_record_arm(&r);
    CHECK(boot_record_take(&r, CSR2_SFTRSTF | CSR2_PINRSTF));
    CHECK(!boot_record_take(&r, CSR2_SFTRSTF | CSR2_PINRSTF));
    /* BOOTLOADER, but the reset becomes a watchdog reset (hang before NVIC_SystemReset): no DFU, cleared. */
    boot_record_arm(&r);
    CHECK(!boot_record_take(&r, CSR2_IWDGRSTF | CSR2_PINRSTF));
    CHECK(!boot_record_take(&r, CSR2_SFTRSTF | CSR2_PINRSTF));
}

int main(void)
{
    RUN(test_reset_token_table);
    RUN(test_reset_token_priority);
    RUN(test_csr2_bit_values);
    RUN(test_boot_magic_value);
    RUN(test_jump_por_random_ram);
    RUN(test_jump_pin_only);
    RUN(test_jump_sw_reset);
    RUN(test_jump_sw_plus_other);
    RUN(test_jump_bad_record);
    RUN(test_record_arm);
    RUN(test_record_take_clears_every_case);
    RUN(test_record_take_counter);
    RUN(test_record_sequence);
    return minitest_summary();
}

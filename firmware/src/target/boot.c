/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "target/boot.h"

#include "core/bootrec.h"
#include "stm32c0xx.h"

#define BOOT_COUNT_MAGIC 0xB0075EEDu

/* Boot counter in no-init RAM (spec 4.4): survives every system reset, not a power cycle. */
typedef struct {
    uint32_t magic;
    uint32_t magic_inv;
    uint32_t count;
} boot_count_rec_t;

static boot_count_rec_t boot_count_rec __attribute__((section(".noinit")));

/* BOOTLOADER request record (spec 4.6). Own input section .noinit.bootreq: the linker script places *(.noinit)
 * first, so the boot counter keeps its documented address 0x20000000 (bringup.md) and this record follows it at
 * 0x2000000C. Not volatile: every access is a call into core/bootrec.c (another translation unit, no LTO), so
 * the stores are done before NVIC_SystemReset() / the jump. */
static boot_record_t boot_req_rec __attribute__((section(".noinit.bootreq")));

/* AN2606 Rev 70 Table 16 (p. 70): the STM32C071 bootloader occupies 28 KB of system memory from 0x1FFF0000;
 * word 0 = initial MSP, word 1 = reset vector (Cortex-M vector table). */
#define SYSMEM_BASE 0x1FFF0000u

static uint32_t csr2_at_boot;
static bool csr2_valid;

/* RCC_CSR2 is read exactly once, then the flags are cleared (spec 4.4). */
static uint32_t csr2_once(void)
{
    if (!csr2_valid) {
        csr2_at_boot = RCC->CSR2;
        csr2_valid = true;
        RCC->CSR2 |= RCC_CSR2_RMVF;
    }
    return csr2_at_boot;
}

const char *boot_read_reset_token(void)
{
    return boot_reset_token(csr2_once());
}

uint32_t boot_count_increment(void)
{
    /* After a power-on / brown-out reset the SRAM content is undefined. It is not read then: with the
     * RAM_PARITY_CHECK option enabled, reading a never-written word raises a parity NMI (RM0490 Rev 6 2.3). */
    const bool power_reset = (csr2_once() & CSR2_PWRRSTF) != 0u;
    boot_count_rec_t *r = &boot_count_rec;

    if (!power_reset && bootrec_magic_valid(r->magic, r->magic_inv, BOOT_COUNT_MAGIC)) {
        r->count++;
    } else {
        r->magic = BOOT_COUNT_MAGIC;
        r->magic_inv = ~BOOT_COUNT_MAGIC;
        r->count = 1u;
    }
    return r->count;
}

bool boot_optr_iwdg_stop_ok(void)
{
    /* RM0490 Rev 6 4.7.6 FLASH_OPTR bit 17 IWDG_STOP: 0 = IWDG frozen in Stop, 1 = running (factory default). */
    return (FLASH->OPTR & FLASH_OPTR_IWDG_STOP) == 0u;
}

/* Jump to the ROM bootloader (spec 4.6 jump recipe, design M-7; AN2606 Rev 70 section 4 p. 36). Runs from
 * Reset_Handler right after a software reset, so RCC, GPIO and all peripherals are in their reset state (no
 * peripheral clock or PLL to disable; the C071 has no PLL) and every GPIO stays in reset state (step 1). */
__attribute__((noreturn)) static void jump_to_system_memory(void)
{
    /* 2. Interrupts off, SysTick off, every NVIC enable and pending bit cleared (AN2606 p. 36: "Disable
     *    interrupts", "Clear pending interrupts"). Cortex-M0+: 32 IRQ lines = ICER[0] / ICPR[0]. */
    __disable_irq();
    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;
    NVIC->ICER[0] = 0xFFFFFFFFu;
    NVIC->ICPR[0] = 0xFFFFFFFFu;

    /* 3. Remap system memory at 0x00000000: SYSCFG clock on first (RM0490 Rev 6 6.4.15 RCC_APBENR2 bit 0
     *    SYSCFGEN, p. 153), read back so the enable has taken effect before the SYSCFG write; then
     *    SYSCFG_CFGR1.MEM_MODE = 01 "System flash memory" (RM0490 Rev 6 9.1.1 bits 1:0, p. 198; 3.1.1 p. 54).
     *    VTOR is still 0 (SystemInit has not run), so the ROM's own vector table is the active one. */
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    (void)RCC->APBENR2;
    SYSCFG->CFGR1 = (SYSCFG->CFGR1 & ~SYSCFG_CFGR1_MEM_MODE) | SYSCFG_CFGR1_MEM_MODE_0;
    (void)SYSCFG->CFGR1;
    __DSB();
    __ISB();

    /* 4. MSP from 0x1FFF0000, interrupts on (AN2606 p. 36: products with USB must re-enable interrupts before
     *    the jump, the bootloader does not), branch to the reset vector at 0x1FFF0004. One asm block: nothing
     *    touches the stack after MSP is switched. Bit 0 of the ROM reset vector is set (Thumb). */
    const uint32_t sp = *(volatile const uint32_t *)SYSMEM_BASE;
    const uint32_t pc = *(volatile const uint32_t *)(SYSMEM_BASE + 4u);
    __asm volatile("msr msp, %0\n"
                   "cpsie i\n"
                   "bx %1\n"
                   :
                   : "r"(sp), "r"(pc)
                   : "memory");
    __builtin_unreachable();
}

void boot_early_check(void)
{
    /* Before .data/.bss init: only registers, the .noinit record and pure core code (no static data).
     * RCC_CSR2 is not cleared here (main() reads it once more for the reset= token, spec 4.4). After a
     * power-on reset the record is written, never read (SRAM parity, RM0490 Rev 6 2.3). */
    if (boot_record_take(&boot_req_rec, RCC->CSR2)) {
        jump_to_system_memory();
    }
}

void boot_request_bootloader_and_reset(void)
{
    __disable_irq();
    boot_record_arm(&boot_req_rec);
    NVIC_SystemReset(); /* CMSIS: DSB before the AIRCR write, so the record is in SRAM first */
}

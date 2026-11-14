/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 STMicroelectronics (stm32c0xx_hal_conf_template.h, lib/stm32c0xx_hal_driver v1.4.1)
 * Modifications Copyright (c) 2026 Martin Faltus (mafaltu) */
/* HAL configuration for USB-DEBUG-CTRL r1 (STM32C071K8). Derived from
 * lib/stm32c0xx_hal_driver/Inc/stm32c0xx_hal_conf_template.h (v1.4.1), reduced to the modules this firmware uses.
 * Clocks are set by clock.c with direct register writes; the HAL provides the 1 ms SysTick (HAL_GetTick),
 * Cortex/NVIC helpers, GPIO definitions and the IWDG driver. */
#ifndef STM32C0xx_HAL_CONF_H
#define STM32C0xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ########################## Module Selection ############################## */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED

/* ########################## Register Callbacks selection ################## */
#define USE_HAL_IWDG_REGISTER_CALLBACKS 0U

/* ########################## Oscillator Values adaptation ################### */
/* No HSE is fitted (crystal-less USB, spec 2); the value only satisfies the HAL headers. */
#if !defined(HSE_VALUE)
#define HSE_VALUE 48000000U
#endif
#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT 100UL
#endif
#if !defined(HSI_VALUE)
#define HSI_VALUE 48000000U /* HSI48 */
#endif
#if !defined(HSI48_VALUE)
#define HSI48_VALUE 48000000U /* HSIUSB48 */
#endif
#if !defined(LSI_VALUE)
#define LSI_VALUE 32000UL
#endif
#if !defined(LSI_STARTUP_TIME)
#define LSI_STARTUP_TIME 130UL
#endif
#if !defined(LSE_VALUE)
#define LSE_VALUE 32768UL
#endif
#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT 5000UL
#endif

/* ########################### System Configuration ######################### */
#define VDD_VALUE                3300UL
#define TICK_INT_PRIORITY        ((1UL << __NVIC_PRIO_BITS) - 1UL) /* lowest */
#define USE_RTOS                 0U
#define PREFETCH_ENABLE          1U
#define INSTRUCTION_CACHE_ENABLE 1U

/* ########################## Includes ###################################### */
#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32c0xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32c0xx_hal_gpio.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32c0xx_hal_cortex.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32c0xx_hal_flash.h"
#endif
#ifdef HAL_IWDG_MODULE_ENABLED
#include "stm32c0xx_hal_iwdg.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32c0xx_hal_pwr.h"
#endif

#define assert_param(expr) ((void)0U)

#ifdef __cplusplus
}
#endif

#endif /* STM32C0xx_HAL_CONF_H */

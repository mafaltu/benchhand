/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "target/board.h"

#include <stdint.h>

#include "stm32c0xx.h"

/* Pin bit masks (port-relative). */
#define PIN(n) (1u << (n))

/* outputs */
#define RLY1_PIN     PIN(5)  /* PA5 */
#define RLY2_PIN     PIN(4)  /* PA4 */
#define RLY3_PIN     PIN(1)  /* PA1 */
#define RLY4_PIN     PIN(0)  /* PA0 */
#define RLY5_PIN     PIN(3)  /* PB3 */
#define LED_USB_PIN  PIN(7)  /* PA7 */
#define DATA_EN_PIN  PIN(8)  /* PA8 */
#define VBUS_EN_PIN  PIN(2)  /* PB2 */

/* inputs */
#define BTN1_PIN     PIN(0)  /* PB0 */
#define BTN2_PIN     PIN(3)  /* PA3 */
#define BTN3_PIN     PIN(14) /* PC14 */
#define BTN4_PIN     PIN(9)  /* PB9 */
#define BTN5_PIN     PIN(8)  /* PB8 */
#define BTN_USB_PIN  PIN(1)  /* PB1 */
#define IN1_PIN      PIN(2)  /* PA2 */
#define IN2_PIN      PIN(7)  /* PB7 */
#define IN3_PIN      PIN(6)  /* PB6 */
#define IN4_PIN      PIN(5)  /* PB5 */
#define PG_OK_PIN    PIN(4)  /* PB4 */
#define OUT_OK_PIN   PIN(15) /* PA15 */

/* analog (unused / programming UART / USB until Task 10) */
#define A_ANALOG (PIN(6) | PIN(9) | PIN(10) | PIN(11) | PIN(12)) /* PA6, PA9, PA10, PA11, PA12 */
#define C_ANALOG (PIN(15) | PIN(6))                               /* PC15, PC6 */

#define A_OUT (RLY1_PIN | RLY2_PIN | RLY3_PIN | RLY4_PIN | LED_USB_PIN | DATA_EN_PIN)
#define B_OUT (RLY5_PIN | VBUS_EN_PIN)
#define A_IN  (BTN2_PIN | IN1_PIN | OUT_OK_PIN)
#define B_IN  (BTN1_PIN | BTN4_PIN | BTN5_PIN | BTN_USB_PIN | IN2_PIN | IN3_PIN | IN4_PIN | PG_OK_PIN)
#define C_IN  (BTN3_PIN)

/* GPIOx_MODER values (RM0490 8.5.1): 00 input, 01 output, 11 analog. */
#define MODE_IN  0u
#define MODE_OUT 1u
#define MODE_AN  3u

/* Spreads a 16-bit pin mask to the matching 2-bit fields of MODER/OSPEEDR/PUPDR. */
static uint32_t field2(uint32_t pins, uint32_t val)
{
    uint32_t r = 0;
    for (uint32_t n = 0; n < 16u; n++) {
        if ((pins & PIN(n)) != 0u) {
            r |= val << (2u * n);
        }
    }
    return r;
}

/* Configures one port. Order: ODR=0 for the outputs, output type/speed, no pull on the used pins, then all mode
 * changes of the port in a single MODER write. Pins not listed keep their reset configuration. */
static void port_init(GPIO_TypeDef *g, uint32_t out, uint32_t in, uint32_t an)
{
    const uint32_t used = out | in | an;

    g->BRR = out;                          /* ODR bits of the outputs = 0 while the pins are still analog */
    g->OTYPER &= ~out;                     /* push-pull */
    g->OSPEEDR &= ~field2(out, 3u);        /* very low speed (00) */
    g->PUPDR &= ~field2(used, 3u);         /* external pulls on every used pin; none internal */
    g->MODER = (g->MODER & ~field2(used, 3u)) | field2(out, MODE_OUT) | field2(in, MODE_IN) | field2(an, MODE_AN);
}

void board_init_safe(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN;
    (void)RCC->IOPENR; /* clock enable takes effect before the first GPIO access */

    port_init(GPIOA, A_OUT, A_IN, A_ANALOG);
    port_init(GPIOB, B_OUT, B_IN, 0u);
    port_init(GPIOC, 0u, C_IN, C_ANALOG);

    /* Spec 2: PA9/PA10 stay on their own pins; the PA11/PA12 -> PA9/PA10 remap must be off (reset value 0,
     * written explicitly). RM0490 9.1.1 SYSCFG_CFGR1 bits 3, 4. */
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    (void)RCC->APBENR2;
    SYSCFG->CFGR1 &= ~(SYSCFG_CFGR1_PA11_RMP | SYSCFG_CFGR1_PA12_RMP);
}

void board_read(app_in_t *in)
{
    const uint32_t a = GPIOA->IDR;
    const uint32_t b = GPIOB->IDR;
    const uint32_t c = GPIOC->IDR;

    /* buttons: active low */
    in->btn_pressed[0] = (b & BTN1_PIN) == 0u;
    in->btn_pressed[1] = (a & BTN2_PIN) == 0u;
    in->btn_pressed[2] = (c & BTN3_PIN) == 0u;
    in->btn_pressed[3] = (b & BTN4_PIN) == 0u;
    in->btn_pressed[4] = (b & BTN5_PIN) == 0u;
    in->btn_pressed[BTN_USB] = (b & BTN_USB_PIN) == 0u;

    /* isolated inputs: GPIO low = input voltage HIGH */
    in->in_high[0] = (a & IN1_PIN) == 0u;
    in->in_high[1] = (b & IN2_PIN) == 0u;
    in->in_high[2] = (b & IN3_PIN) == 0u;
    in->in_high[3] = (b & IN4_PIN) == 0u;

    /* passthrough status: high = OK */
    in->pg_ok = (b & PG_OK_PIN) != 0u;
    in->out_ok = (a & OUT_OK_PIN) != 0u;
}

/* BSRR: low half sets, high half resets (RM0490 8.5.7). */
static uint32_t bsrr(uint32_t pin, bool on)
{
    return on ? pin : (pin << 16);
}

void board_write(const app_out_t *out)
{
    const uint8_t m = out->relay_mask;
    const uint32_t a = bsrr(RLY1_PIN, (m & 0x01u) != 0u) | bsrr(RLY2_PIN, (m & 0x02u) != 0u) |
                       bsrr(RLY3_PIN, (m & 0x04u) != 0u) | bsrr(RLY4_PIN, (m & 0x08u) != 0u) |
                       bsrr(LED_USB_PIN, out->led_usb) | bsrr(DATA_EN_PIN, out->data_en);
    const uint32_t b = bsrr(RLY5_PIN, (m & 0x10u) != 0u) | bsrr(VBUS_EN_PIN, out->vbus_en);

    GPIOA->BSRR = a;
    GPIOB->BSRR = b;
}

void board_uid_hex(char out[25])
{
    static const char hex[] = "0123456789ABCDEF";
    const volatile uint32_t *uid = (const volatile uint32_t *)UID_BASE;
    unsigned k = 0;

    for (unsigned w = 0; w < 3u; w++) {
        const uint32_t v = uid[w];
        for (int s = 28; s >= 0; s -= 4) {
            out[k++] = hex[(v >> (unsigned)s) & 0xFu];
        }
    }
    out[k] = '\0';
}

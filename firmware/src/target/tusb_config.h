/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

/* TinyUSB 0.21.0 configuration, USB-DEBUG-CTRL r1 (STM32C071K8, USB FS device, one CDC ACM interface, no RTOS).
 * Included by lib/tinyusb/src/tusb_option.h after the OPT_* constants are defined. */

#define CFG_TUSB_MCU          OPT_MCU_STM32C0
#define CFG_TUSB_OS           OPT_OS_NONE
#define CFG_TUSB_DEBUG        0
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

#define CFG_TUD_ENABLED        1
#define CFG_TUD_MAX_SPEED      OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_CDC    1
#define CFG_TUD_MSC    0
#define CFG_TUD_HID    0
#define CFG_TUD_MIDI   0
#define CFG_TUD_VENDOR 0

#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 256
#define CFG_TUD_CDC_RX_EPSIZE  64
#define CFG_TUD_CDC_TX_EPSIZE  64

/* M-3 / FW-23: replies never depend on DTR. The default 1 (class/cdc/cdc_device.h:84-85) makes the TX FIFO
 * overwritable while DTR is low (cdc_device.c:428-434), so unread reply bytes would be overwritten instead of
 * back-pressuring the app. 0 keeps the FIFO non-overwritable in every line state. */
#define CFG_TUD_CDC_TX_OVERWRITABLE_IF_NOT_CONNECTED 0

/* ES0618 Rev 2 2.11.1: wait >= 800 ns after CTR before reading the packet memory of an OUT transfer.
 * TinyUSB runs fsdev_btable_workaround_delay() (portable/st/stm32_fsdev/fsdev_stm32.h:312-317, 11 cycles per loop)
 * on every CTR_RX for FSDEV 32-bit devices (dcd_stm32_fsdev.c:447-449). Its default count
 * 48 MHz / 13.75 MHz = 3 loops (fsdev_stm32.h:292-294) = 33 cycles = 687.5 ns at 48 MHz, short of 800 ns.
 * 4 loops = 44 cycles = 916.7 ns. Checked by _Static_assert in usb_glue.c. */
#define CFG_TUSB_FSDEV_BTABLE_FS_DELAY_COUNT 4

#endif

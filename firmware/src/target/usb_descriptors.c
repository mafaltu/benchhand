/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu)
 * Structure follows the TinyUSB cdc_msc example (MIT License, Copyright (c) 2019 Ha Thach, tinyusb.org). */
/* USB descriptors, USB-DEBUG-CTRL r1: one CDC ACM function (IAD), full speed, bus-powered (spec 5.6, FW-20/24).
 * Pattern: lib/tinyusb/examples/device/cdc_msc/src/usb_descriptors.c (TinyUSB 0.21.0). All identity values come
 * from usb_ids.h. */
#include <stddef.h>
#include <stdint.h>

#include "target/board.h"
#include "target/usb_ids.h"
#include "tusb.h"

/* ---------- device ---------- */

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    /* IAD (TUD_CDC_DESCRIPTOR carries one): class MISC / common / IAD as the USB IAD ECN requires */
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_ID_VID,
    .idProduct = USB_ID_PID,
    .bcdDevice = USB_ID_BCD_DEVICE,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&desc_device;
}

/* ---------- configuration ---------- */

enum { ITF_NUM_CDC = 0, ITF_NUM_CDC_DATA, ITF_NUM_TOTAL };

enum { STRID_LANGID = 0, STRID_MANUFACTURER, STRID_PRODUCT, STRID_SERIAL, STRID_INTERFACE, STRID_COUNT };

/* FSDEV allows the same endpoint number in both directions. */
#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    USB_EP_CDC_IN /* usb_ids.h: also polled by usb_glue_tx_drained() */

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

/* bmAttributes: bit 7 set by the macro; self-powered (bit 6) and remote wakeup (bit 5) are 0.
 * bMaxPower = USB_ID_MAX_POWER_MA / 2 units of 2 mA. */
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, USB_ID_MAX_POWER_MA),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_INTERFACE, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN,
                       CFG_TUD_CDC_TX_EPSIZE),
};

_Static_assert(sizeof(desc_configuration) == CONFIG_TOTAL_LEN, "configuration descriptor length");
_Static_assert(USB_ID_MAX_POWER_MA <= 500u && (USB_ID_MAX_POWER_MA % 2u) == 0u, "bMaxPower");
_Static_assert(CFG_TUD_CDC_TX_EPSIZE == CFG_TUD_CDC_RX_EPSIZE, "one bulk size for both directions");

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

/* ---------- strings ---------- */

static const char *const string_ascii[STRID_COUNT] = {
    [STRID_MANUFACTURER] = USB_ID_MANUFACTURER,
    [STRID_PRODUCT] = USB_ID_PRODUCT,
    [STRID_SERIAL] = NULL, /* runtime: UID */
    [STRID_INTERFACE] = USB_ID_INTERFACE,
};

#define STR_MAX_CHARS 40u /* longest string: USB_ID_INTERFACE, 34 chars */
_Static_assert(sizeof(USB_ID_MANUFACTURER) - 1u <= STR_MAX_CHARS, "manufacturer string too long");
_Static_assert(sizeof(USB_ID_PRODUCT) - 1u <= STR_MAX_CHARS, "product string too long");
_Static_assert(sizeof(USB_ID_INTERFACE) - 1u <= STR_MAX_CHARS, "interface string too long");

static uint16_t desc_str[1u + STR_MAX_CHARS]; /* UTF-16LE; [0] = bLength | bDescriptorType << 8 */

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t n = 0;

    if (index == STRID_LANGID) {
        desc_str[1] = 0x0409; /* English (United States) */
        n = 1;
    } else if (index == STRID_SERIAL) {
        char uid[25];
        board_uid_hex(uid);
        for (; n < 24u; n++) {
            desc_str[1u + n] = (uint16_t)(uint8_t)uid[n];
        }
    } else if (index < STRID_COUNT) {
        const char *s = string_ascii[index];
        for (; s[n] != '\0' && n < STR_MAX_CHARS; n++) {
            desc_str[1u + n] = (uint16_t)(uint8_t)s[n];
        }
    } else {
        return NULL; /* unknown index (e.g. 0xEE MS OS descriptor): STALL */
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * n + 2u));
    return desc_str;
}

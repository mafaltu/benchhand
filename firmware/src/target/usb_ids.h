/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef TARGET_USB_IDS_H
#define TARGET_USB_IDS_H

/* USB identity (spec 5.6, FW-20, FW-24). The single place to change it: only the PID is a placeholder
 * until pid.codes assigns one (strings: product BenchHand, repository github.com/mafaltu/benchhand).
 * The manufacturer, product and serial strings form the Linux name
 * /dev/serial/by-id/usb-<MANUFACTURER>_<PRODUCT>_<serial>-if00. */

#define USB_ID_VID          0x1209u /* pid.codes */
#define USB_ID_PID          0x0001u /* pid.codes test PID, placeholder (FW-20) */
#define USB_ID_BCD_DEVICE   0x0010u /* device release 0.1.0 */
#define USB_ID_MAX_POWER_MA 250u    /* control-side budget (A_control.md); bus-powered, no remote wakeup */

#define USB_ID_MANUFACTURER "mafaltu"
#define USB_ID_PRODUCT      "BenchHand"
/* iInterface of the CDC control interface: shown by lsusb -v and in the udev attributes (FW-24). */
#define USB_ID_INTERFACE    "BenchHand control - send HELP"
/* CDC data IN endpoint (bulk). usb_glue_tx_drained() checks that it is idle before a reset (Task 12). */
#define USB_EP_CDC_IN 0x82

/* Serial: the 96-bit MCU UID as 24 upper-case hex chars, built at runtime by board_uid_hex() (FW-12). */

#endif

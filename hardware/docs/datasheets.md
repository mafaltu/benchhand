<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
Copyright (c) 2026 Martin Faltus (mafaltu)

# BenchHand r1 - datasheet links

Links to the official manufacturer product or datasheet pages for every IC, discrete semiconductor, connector and
electromechanical part in the [BOM](../bom/benchhand_r1_bom.csv). Datasheets are not redistributed in this
repository; always download the current revision from the manufacturer. Passive parts (resistors, capacitors) are
generic: any part meeting the key spec in the BOM is fine.

Where a cell says "manufacturer site", no exact product page could be confirmed; search the part number on that
domain.

## ICs

| Refs | Part | Manufacturer | MPN | Official page |
|---|---|---|---|---|
| U101 | Arm Cortex-M0+ MCU with crystal-less USB FS | STMicroelectronics | STM32C071K8T6 | https://www.st.com/en/microcontrollers-microprocessors/stm32c071k8.html |
| U104 | 3.3 V LDO regulator, 250 mA, 16 V | Microchip | MCP1703AT-3302E/CB | https://www.microchip.com/en-us/product/mcp1703a |
| U401 | eFuse, latch-off circuit breaker, reverse blocking, power good | Texas Instruments | TPS259474LRPWR | https://www.ti.com/product/TPS25947 |
| U402 | USB 2.0 High Speed 1:2 analog switch | Texas Instruments | TS3USB221ERSER | https://www.ti.com/product/TS3USB221E |
| U403 | dual Schmitt-trigger buffer | Texas Instruments | SN74LVC2G17DCKR | https://www.ti.com/product/SN74LVC2G17 |
| U311 U321 U331 U341 | opto-emulator switch (isolated input) | Texas Instruments | ISOM8600DFGR | https://www.ti.com/product/ISOM8600 |
| U102 D403 D404 | 2-channel low-capacitance USB ESD protection | Texas Instruments | TPD2E2U06DRLR | https://www.ti.com/product/TPD2E2U06 |

## Discrete semiconductors and LEDs

| Refs | Part | Manufacturer | MPN | Official page |
|---|---|---|---|---|
| D101 | 1-channel bidirectional ESD clamp (control VBUS) | Texas Instruments | TPD1E10B06DYAR | https://www.ti.com/product/TPD1E10B06 |
| Q201..Q205, Q401 | 20 V N-channel MOSFET, SOT-23 | Diodes Incorporated | DMN2056U-7 | https://www.diodes.com/part/view/DMN2056U |
| D201..D205, D311 D321 D331 D341 | small-signal switching diode | Diotec Semiconductor | 1N4148W | https://diotec.com/en/product/1N4148W.html |
| D401 D402 | unidirectional TVS, 6.0 V standoff | Diotec Semiconductor | SMF6.0A | https://diotec.com/request/datasheet/smf50a.pdf (SMF5.0 ... SMF220CA series datasheet) |
| LED101, LED201..LED205 | 3 mm green diffused THT LED, low current | Kingbright | L-934LGD | manufacturer site: kingbright.com |

## Connectors

| Refs | Part | Manufacturer | MPN | Official page |
|---|---|---|---|---|
| J101 | USB-C 2.0 receptacle, 16 pins, top mount | GCT | USB4105-GF-A | https://gct.co/connector/usb4105 |
| J401 | USB-B 2.0 receptacle, THT right angle | Molex | 0670688000 (67068-8000) | https://www.molex.com/en-us/products/part-detail/670688000 |
| J402 | USB-A 2.0 receptacle, THT right angle | Molex | 67643-0910 | https://www.molex.com/en-us/products/part-detail/676430910 |
| J201..J205 | 3-position screw terminal, 5.0 mm pitch | Same Sky | TB002-500-03BE | https://www.sameskydevices.com/product/interconnect/connectors/terminal-blocks/tb002-500-series |
| J301 | 2x4 right-angle header, 2.54 mm | Samtec | TSW-104-08-F-D-RA | https://www.samtec.com/products/tsw-104-08-f-d-ra |
| J301 (alternate) | same header, matte tin | Samtec | TSW-104-08-T-D-RA | https://www.samtec.com/products/tsw-104-08-t-d-ra |

## Electromechanical

| Refs | Part | Manufacturer | MPN | Official page |
|---|---|---|---|---|
| K201..K205 | signal relay, 5 V coil, DPDT | Hongfa | HFD4/5 | https://www.hongfa.com/product/signal-relay/HFD4 (HFD4 series) |
| SW101..SW106 | tactile switch 6x6 mm, 13 mm actuator, 160 gf | Same Sky | TS02-66-130-BK-160-LCR-D | https://www.sameskydevices.com/product/switches/tactile-switches/ts02-series (series page; datasheet https://www.sameskydevices.com/product/resource/ts02.pdf) |

## Tools and reference documents

| Item | Manufacturer | Official page |
|---|---|---|
| Tag-Connect TC2050-IDC-NL programming cable (for the J102 pads; not a BOM part) | Tag-Connect | manufacturer site: tag-connect.com |
| STM32C071x8/xB device errata (ES0618) | STMicroelectronics | https://www.st.com/resource/en/errata_sheet/es0618-stm32c071x8xb-device-errata-stmicroelectronics.pdf |
| STM32C0 reference manual (RM0490) and system memory boot mode note (AN2606) | STMicroelectronics | manufacturer site: st.com |

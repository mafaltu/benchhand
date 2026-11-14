<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (c) 2026 Martin Faltus (mafaltu) -->
# HIL results, board #1 (UID 005900A75943501420363441), 2026-11-14

Firmware: debug build `-DFW_DEBUG_UART=ON` at 3b004e3 (fw 0.1.0), ST-Link V3SET FW V3J16M9B5S1, CubeProgrammer 2.23.0.

| bringup item | result |
|---|---|
| 0. visual, ohm checks, first power, rails, LEDs | PASS (owner, no defect found) |
| 0. SWD connect without VTref | PASS: Device ID 0x493 STM32C07x Rev Z, 64 KB, BL 0xD1; CLI shows `Voltage 0.00V`, works anyway (same TC2050 cabling as the owner's other boards); first connect attempt usually fails, second succeeds |
| 0. factory option bytes | RDP 0xAA, IWDG_STOP 1, nBOOT_SEL 1, nBOOT0 1, nBOOT1 1, NRST_MODE 3, BOR_EN 0 |
| 0. ROM DFU on blank MCU | not seen on USB (nBOOT_SEL=1, nBOOT0=1 -> empty-flash check not used); note only |
| 2. flash | image correct (100 kHz readback 2x identical to .bin); **SWD signal integrity issue**: erase failed at 8 MHz, verify read 1 bit wrong at 1 MHz (0x08007B58 0x61 vs 0x65). Use freq=100 until investigated (scope SWCLK/SWDIO, try VTref from TP102) |
| 2. optr_err=1 with factory OB | PASS (STATUS? and UART banner) |
| 2. IWDG_STOP=0 | PASS: programmed at 100 kHz, readback 0x0, RDP still 0xAA, `optr_err=0` after reload |
| 3/4. clocks + USB enumeration | PASS: `usb configured` ~380 ms after reset, enumerates as 1209:0001, /dev/serial/by-id/usb-<old manufacturer>_USB-DEBUG-CTRL_005900A75943501420363441-if00 (identity before FW-33/FW-34) |
| debug UART (FW-32) | PASS: banner, usb events, io diffs on ST-Link VCP -if02 at 115200 |
| buttons BTN1..5 + BTN_USB | PASS: each seen in io log; BTN1..5 drive their relay while held; counters 4,4,6,4,13 / usb 1 |
| relays via CDC | PASS: SET n ON/OFF 1..5, SET ALL ON with 10 ms stagger (01-03-07-0F-1F), no fault; owner confirmed click, LED201..205, and contacts COM-NO / COM-NC on J201..J205 (10 s per relay) |
| passthrough without source | as designed: VBUS_EN 100 ms pulse every 2 s, `usb=no_input` |
| LED_USB | on only in passthrough state ON (as designed); off while usb=no_input |
| passthrough power sequence | PASS: VBUS_EN -> OUT_OK +8..9 ms -> PG_OK +6..7 ms -> DATA_EN/LED +12 ms (~27 ms total); off: DATA first, VBUS +1 ms, OUT_OK low after 7 ms; USB button: off ~1 s then back, fault=none |
| passthrough data, USB stick IS918 on USB-A, USB-B to the PC | PASS: enumerates at 480M (high-speed). USB OFF: gone in <0.05 s; USB ON: back in 0.70 s; USB DATA OFF: gone, usb=power; USB DATA ON: back in 0.08 s; USB DISCONNECT 3000: gone, back after 3.67 s; fault=none throughout |
| USB stick safety (owner remark) | the test stick was auto-mounted; switching it off unmounted gave `FAT-fs: Volume was not properly unmounted` + 1 lost async write (dirty-flag write). Owner checked the stick: OK. Rule: passthrough tests use a non-storage device or `udisksctl unmount` first; HELP/README note for agents proposed |
| REBOOT | PASS: `OK` arrives 11 ms after the command, then disconnect; back in ~0.4 s with `reset=sw`, boot count +1. pyserial raises SerialException on the disconnect and drops the already received `OK` (host-side; raw os.read sees it) -> README note for agents |
| BOOTLOADER -> ROM DFU | PASS: `OK`, 0483:df11 after ~0.6 s (serial 208F34E85943, alt 0 Internal Flash 64*02Kg, alt 1 OB, alt 2 ENGI, alt 3 OTP) |
| Q-2 DFU `:leave` | PASS: `dfu-util -a 0 -s 0x08000000:leave -D usb_debug_ctrl.bin` downloads, prints `Error during download get_status` after the leave request (expected, the device left), app back as 1209:0001 after ~0.5 s with `reset=sw boot=1` (ROM uses SRAM); SWD readback identical to the .bin |
| isolated inputs IN1..IN4, 5 V from a USB power bank on J301 | PASS: each channel alone gives exactly its bit (IN1 1,0,0,0 / IN2 0,1,0,0 / IN3 0,0,1,0 / IN4 0,0,0,1), all together 1,1,1,1, removed 0,0,0,0; contact bounce while wiring visible in the raw io log, STATUS debounced |
| IN4 reverse polarity, 5 V | PASS: reads 0 (`in=0,0,0,0`, no io event while reversed), clamp diode path as designed (C_inputs.md). Inputs are rated max 12 V (24 V overheats the 1206 series resistors, R-C-24V) |
| PC sleep (S3) with debug build | functional PASS: usb suspend -> DATA off, VBUS +1 ms, OUT_OK low +4 ms -> `enter stop` -> on PC wake `woke from Stop 0, clock restore ok`, `usb resume`, passthrough back to on in 28 ms; STATUS suspends=1 lp_err=0 fault=none. **BUG**: resume noise-wake hold (APP_RESUME_CONFIRM_MS 10 ms) defeated - VBUS on in the same ms as `usb resume`, because USB events are timestamped with the pre-Stop `last` tick (main.c usb_glue_task(&app, last)); fix dispatched. Also 4 bytes lost in the UART log at the suspend line (likely host/ST-Link VCP suspending; unverified) |
| PC sleep re-test after fix 322a668 (debug build) | PASS: `usb resume` at 140441, VBUS_EN stays 0 until 140451 (10 ms hold), then OUT_OK +8, PG_OK +7, DATA/LED +12 ms; suspends=1 lp_err=0 fault=none. UART log again lost exactly 4 bytes right after the `usb suspend` line (both sleeps) - consistent with the ST-Link VCP / host suspending at that moment; firmware-side cause not excluded (minor, debug only) |
| final image for now | normal build (FW_DEBUG_UART OFF) at f6e89c9, flashed over USB DFU with `:leave`: usb=on, fault=none, optr_err=0, new HELP USB storage line present. Full debug UART log of the session: `hil-uart-2026-11-14.log` |

## Open HIL items (owner has no scope / load / USB meter right now)
- suspend current < 2.5 mA (normal build, TC2050 off)
- power-up glitch scope check on RLY gates (10 power-ups)
- passthrough 1.5 A load, HS signal quality
- SWD at full clock (scope SWCLK/SWDIO, VTref from TP102)
- inputs at 3.0 V (ON threshold) and 12 V (upper limit)
| FW-33 manufacturer `MaFaltu` | build at working tree after f6e89c9 + FW-33 flashed over DFU `:leave`: `lsusb -v` iManufacturer MaFaltu, iProduct USB-DEBUG-CTRL, iSerial 005900A75943501420363441; port `/dev/serial/by-id/usb-MaFaltu_USB-DEBUG-CTRL_005900A75943501420363441-if00`; STATUS usb=on optr_err=0 |
| FW-34 identity BenchHand | flashed over DFU `:leave`: iManufacturer mafaltu, iProduct BenchHand, iInterface (sysfs) `BenchHand control - send HELP` (`lsusb -v` without root shows it empty); port `/dev/serial/by-id/usb-mafaltu_BenchHand_005900A75943501420363441-if00`; ID? model=USB-DEBUG-CTRL hw=r1 |
| FW-39 model=BenchHand | c485b92 flashed over DFU `:leave`: `ID?` -> `OK model=BenchHand hw=r1 fw=0.1.0 proto=1 sn=005900A75943501420363441`, HELP line 1 `# BenchHand r1 - USB-controlled hardware test helper for AI agents.` |

## Second board: UID 004400A95943501420363441 (owner's "board 1"), 2026-11-14 afternoon

Note on numbering: "board #1" above is the board with UID 005900A75943501420363441 (the first one tested). A third
board had the MCU rotated by 90 deg (assembly error; owner reworks it, then repeats section 0 - the MCU was powered
in the wrong orientation and may be damaged).

| bringup item | result |
|---|---|
| state as received | blank flash (FFFFFFFF), enumerates as ROM DFU 0483:df11 (serial 207A34EA5943) -> USB path and MCU supply OK; factory OB (IWDG_STOP 1, RDP 0xAA) |
| SWD | PASS at 100 kHz, first attempt (Device ID 0x493, BL 0xD1) |
| flash | DFU attempt found no device (an SWD `mode=UR` read just before had reset the MCU and the ROM DFU did not re-enumerate); flashed over SWD freq=100 instead: verified; 6898dfd image (normal build) |
| IWDG_STOP=0 | PASS, readback 0x0, optr_err=0 |
| USB CDC | PASS: `usb-mafaltu_BenchHand_004400A95943501420363441-if00`, ID? model=BenchHand |
| relays 1..5 + ALL | PASS: owner heard all click; ALL ON -> rly=1,1,1,1,1, fault=none |
| buttons 1..5 + USB | PASS: owner pressed each twice -> counters +2 each (one earlier count on button 5 = an extra press before the test) |
| inputs IN1..IN4, 5 V power bank, one at a time | PASS: 1,0,0,0 / 0,1,0,0 / 0,0,1,0 / 0,0,0,1 (edge counters show contact bounce while wiring) |
| passthrough, Logitech receiver 046d:c542 (full speed 12M) on USB-A, USB-B to the PC | PASS: usb=on; USB OFF gone 0.08 s / USB ON back 0.42 s; USB DATA OFF gone, usb=power / DATA ON back 0.41 s; USB DISCONNECT 3000 gone, back 3.35 s; fault=none. High-speed (480M) not checked on this board (full-speed device) |

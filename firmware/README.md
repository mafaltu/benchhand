# BenchHand firmware (r1 board)

BenchHand (repository: https://github.com/mafaltu/benchhand) is a USB-controlled hardware test helper; this is its firmware for the BenchHand r1 board (STM32C071K8T6, 64 KB flash, 24 KB RAM), made for AI
agents. The board has 5 relays, 4 isolated inputs, 6 buttons and a USB 2.0 passthrough. The agent talks directly to a USB CDC
serial port with a plain text protocol; the board describes itself (`HELP`). Design spec:
`docs/superpowers/specs/2026-11-14-firmware-design.md`; decisions: `firmware/DECISIONS.md` (FW-xx); hardware pin map:
`docs/firmware_onboarding.md`; bench checks: `firmware/docs/bringup.md`.

All commands below are run from the repository root.

## Inputs (IN1..IN4)

Isolated, current-driven: the DUT signal must source at least about 1.3 mA. HIGH is guaranteed from 3.0 V; the maximum
is **12 V** (24 V overheats the series resistors, design risk R-C-24V). Reverse polarity reads 0 (verified at 5 V).

## Toolchain and pinned sources

`firmware/scripts/check_toolchain.sh` prints the version of every tool and exits 1 if one is missing. Versions used
on the build PC:

- Arm GNU Toolchain 15.2.Rel1 (arm-none-eabi-gcc 15.2.1 20251203); `ARM_TOOLCHAIN_DIR` defaults to
  `$HOME/.pico-sdk/toolchain/15_2_Rel1` (the script also uses an `arm-none-eabi-gcc` already on `PATH`).
- STM32CubeProgrammer CLI v2.23.0 (`STM32_PROGRAMMER_CLI`, default
  `$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI`).
- host gcc 13.3.0, cmake 3.28.3 (>= 3.20 required), dfu-util 0.11.

Git submodules under `firmware/lib/` (fetch with `git submodule update --init`):

| Path | Tag | Commit |
|---|---|---|
| `firmware/lib/tinyusb` | 0.21.0 | dae3f9a366bf |
| `firmware/lib/stm32c0xx_hal_driver` | v1.4.1 | 038bed9f00e0 |
| `firmware/lib/cmsis_device_c0` | v1.4.1 | bcc8d94fecb7 |
| `firmware/lib/CMSIS_5` | 5.9.0 | 2b7495b8535b |

## Build

Host unit tests (pure modules in `firmware/src/core/` plus the FW-32 log queue `src/target/dbg_ring.c`, ASan +
UBSan, 11 tests):

```sh
cmake -S firmware -B firmware/build-host -DFW_TARGET=OFF && cmake --build firmware/build-host && ctest --test-dir firmware/build-host --output-on-failure
```

Target image (output `firmware/build-arm/usb_debug_ctrl.elf` / `.hex` / `.bin`; the link prints the memory use,
about 41 KB flash of 64 KB and 3.6 KB RAM of 24 KB):

```sh
cmake -S firmware -B firmware/build-arm -DFW_TARGET=ON -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake && cmake --build firmware/build-arm
```

Optional cache variables: `-DFW_VERSION=0.1.0` (shown by `ID?`), `-DREPO_URL=<url>` (shown by `HELP`),
`-DFW_TEST_STALL_MS=5000` (DEBUG ONLY: stops refreshing the watchdog, see `docs/bringup.md`; never leave such an
image on a board).

## Debug UART (bring-up builds)

DEBUG ONLY (FW-32). `-DFW_DEBUG_UART=ON` adds a one-way text log on USART1 TX = PA9 (TC2050 pad PROG_TX), 115200
8N1, no RX (PA10 stays analog). The default build (OFF) has no console and is binary-identical to a build without
this feature; CMake prints a warning when it is ON. Use a separate build directory:

```sh
cmake -S firmware -B firmware/build-arm-dbg-on -DFW_TARGET=ON -DFW_DEBUG_UART=ON && cmake --build firmware/build-arm-dbg-on
```

Connect the TC2050 cable of the ST-Link V3SET; its virtual COM port (VCP) receives PA9. The VCP appears as
`/dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_*-if0?` (the `*` is the probe serial and `?` the interface
number; if the glob matches more than one device, e.g. two probes, check `ls /dev/serial/by-id/` and use the full
name). View the log with either:

```sh
picocom -b 115200 /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_*-if0?
stty -F /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_*-if0? 115200 raw; cat /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_*-if0?
```

Every line starts with the millisecond tick. Only events are logged (boot, USB events, Stop entry / wake, REBOOT /
BOOTLOADER, and one `io` line whenever an input or an output changed); CDC command text is not logged. Example
(illustrative, not captured from hardware):

```text
[1] BenchHand fw 0.1.0 reset=por boot=1 optr_err=1 uid=0123456789ABCDEF01234567
[1] usb init done
[2] io btn=00 in=0 pg=0 ok=0 | rly=00 vbus=0 data=0 led=0
[160] usb bus_reset
[230] usb configured
[231] io btn=00 in=0 pg=0 ok=0 | rly=00 vbus=0 data=0 led=1
[5012] io btn=01 in=0 pg=0 ok=0 | rly=00 vbus=0 data=0 led=1
```

`io` fields: `btn` bit i = button i+1 pressed (bit 5 = USB button), `in` bit i = input IN(i+1) HIGH, `pg` / `ok` =
passthrough status inputs, `rly` = relay mask (bit 0 = RLY1), `vbus` / `data` / `led` = outputs. A bouncing button
gives several lines; when the 512-byte buffer is full, whole lines are dropped and `[dbg] dropped N` follows.

Warning: the suspend current (< 2.5 mA) must be measured with the normal build (FW_DEBUG_UART OFF) and with the VCP
not driving PA9 (TC2050 cable unplugged). In the debug build PA9 is driven high by the USART also in Stop, and the
log flush delays Stop entry by up to 60 ms.

## One-time option byte: `IWDG_STOP = 0`

The factory default is `IWDG_STOP = 1`: the independent watchdog keeps running in Stop mode. The firmware sleeps in
Stop whenever the PC sleeps (USB suspend), and the watchdog (about 500 ms) would then reset the board in every PC
sleep. So the firmware reads the option byte at start, reports `optr_err=1` in `STATUS?` and never enters Stop while
`IWDG_STOP = 1`. Order on a new board: flash the image (section Flashing, ST-Link), enumerate and check that
`STATUS?` shows `optr_err=1` (factory state, USB-B and any DUT disconnected), then set the option byte once, with the
ST-Link on the SWD pads (J102):

```sh
$STM32_PROGRAMMER_CLI -c port=SWD freq=100 mode=UR -ob IWDG_STOP=0
$STM32_PROGRAMMER_CLI -c port=SWD freq=100 mode=UR -ob displ     # read back: IWDG_STOP = 0x0
```

(`STM32_PROGRAMMER_CLI` = the CubeProgrammer path above.) After the board re-enumerates (re-plug the control USB if needed), `STATUS?` must show `optr_err=0`.

## Flashing

Over USB (DFU, no ST-Link; primary path, verified on hardware): send `BOOTLOADER` to the control port (reply `OK`, the
board turns everything off and jumps to the ROM loader; `dfu-util -l` lists `[0483:df11]`, about 0.6 s later), then:

```sh
dfu-util -a 0 -s 0x08000000:leave -D firmware/build-arm/usb_debug_ctrl.bin
```

`:leave` starts the application right after the download: it returns as `1209:0001` within about 0.5 s
(`reset=sw`). `dfu-util` prints `Error during download get_status` after the leave request; this is expected (the
device has already left DFU). `boot=` in `STATUS?` restarts at 1 after a DFU session (the ROM loader uses SRAM). The
DFU path does not set the option byte; do that step once with the ST-Link. Fallback if the board does not come back:
a power cycle (re-plug the control USB) or `$STM32_PROGRAMMER_CLI -c port=SWD freq=100 mode=UR -rst` with an ST-Link
attached.

With ST-Link (also for a blank chip), SWD at 100 kHz (`freq=100`). The J102 TC2050 pads fit a generic TC2050-IDC-NL
cable, but the pinout is custom: probe-specific cables (e.g. the ST-LINK STDC14 one) do not work. Wire the cable to
the probe as in
[hardware/docs/README.md#adapter-wiring](../hardware/docs/README.md#adapter-wiring):

```sh
$STM32_PROGRAMMER_CLI -c port=SWD freq=100 mode=UR -w firmware/build-arm/usb_debug_ctrl.hex -v -rst
```

SWD note: on board #1 SWD was unreliable at high clock (erase failed at 8 MHz, a read bit error at 1 MHz) while
100 kHz was 100 % reliable, so use `freq=100` for SWD flashing and readback until this is investigated (HIL open item
"SWD at full clock"). The first connect attempt often fails and the second succeeds. The CLI shows `Voltage 0.00V`
(no VTref on the TC2050 pads, D-34) and works anyway.

## USB identity

Defined only in `firmware/src/target/usb_ids.h`: VID 0x1209 (pid.codes), PID 0x0001 (pid.codes test PID, a placeholder
until a PID is assigned), manufacturer `mafaltu`, product `BenchHand`, serial = 96-bit MCU UID as 24 upper-case hex
chars, iInterface `BenchHand control - send HELP`. The port is therefore
`/dev/serial/by-id/usb-mafaltu_BenchHand_<UID>-if00`; check the real name with `ls /dev/serial/by-id/`. The `ID?` reply
is `OK model=BenchHand hw=r1 fw=<version> proto=1 sn=<UID>` (FW-39; the r1 board silkscreen still reads
"USB-DEBUG-CTRL r1", the hardware name, but the product/model is BenchHand). Enumerated as `1209:0001`
on board #1 (about 380 ms after reset); remaining checks: docs/bringup.md.

## For AI agents: paste-in paragraph

A standalone quickstart for agents (port discovery, framing, example session, safety rules) is in
[`docs/agent_quickstart.md`](docs/agent_quickstart.md); `HELP` links to the public README section `#for-ai-agents`.
Full protocol reference (every HELP page, generated from the firmware source): [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

Copy this into the agent's instructions and fill in the wiring template. It is the whole onboarding; everything else
comes from `HELP`.

> A BenchHand r1 board is connected to this computer. It has 5 relays, 4 digital inputs and a USB passthrough that
> you control over a serial port. The port is `/dev/serial/by-id/usb-mafaltu_BenchHand_*-if00` (the `*` is the
> board's serial number; if several boards are present, ask me which one). First run
> `stty -F <port> raw -echo` (otherwise the tty echoes the board's replies back), then send the line `HELP` and read
> until a line starting with `OK` or `ERR`; `HELP <command>` gives details and examples for each command. Use
> channel numbers only. Wiring for this test setup:
> relay 1 = ..., relay 2 = ..., relay 3 = ..., relay 4 = ..., relay 5 = ...;
> input 1 = ..., input 2 = ..., input 3 = ..., input 4 = ...;
> passthrough = ... (what is plugged into USB-B and USB-A).
> If this computer autosuspends the board's USB device (e.g. TLP or powertop) while no program has the port open,
> everything switches off like in PC sleep (`suspends=` in `STATUS?` counts it); keep it awake with
> `power/control=on` (the udev rule below), or keep the port open. If several programs may use the port, wrap each
> use in `flock <port> <command>`.

Example filled in: `relay 1 = ESP32 BOOT (GPIO0) to GND; relay 2 = ESP32 EN to GND; input 1 = ESP32 LED pin
(3.3 V = HIGH); passthrough = host USB-B in, ESP32 programmer on USB-A`.

### Agent notes (verified on hardware)

- `REBOOT` / `BOOTLOADER`: the board replies `OK` (about 11 ms later) and then disconnects. pyserial raises
  `SerialException` on the disconnect and drops the already received `OK` from its buffer. Read in small chunks or
  with raw `os.read` and treat the disconnect as expected, or verify by re-opening the port and sending `STATUS?`
  (`reset=sw`). Why: pyserial `read(n)` loops until n bytes; after `OK` the port reports readable with 0 bytes
  (disconnect), pyserial raises and its local buffer with `OK` is lost. `readline()` returns at the `\n` before the
  disconnect, so `p.readline()` gets the `OK`.
- USB passthrough: `USB OFF`, `USB DISCONNECT`, `USB DATA OFF`, the USB button and host sleep remove the device on
  USB-A without a safe eject. Harmless for programmers, serial adapters and HID; for a mass-storage device unmount it
  first (`udisksctl unmount -b /dev/sdX1`), otherwise the filesystem can be left dirty (observed: `FAT-fs: Volume was
  not properly unmounted`).

### Keep the board awake (Linux USB autosuspend)

Host sleep, including USB runtime autosuspend of the closed port, turns every relay and the passthrough off by
design (FW-08). On hosts that enable autosuspend (`power/control` = `auto`: TLP `USB_AUTOSUSPEND=1`, `powertop
--auto-tune`, many laptop distributions) a command such as `SET 1 ON` sent by a program that then closes the port
is undone about 2 s later. Pin the board awake with a udev rule, e.g.
`/etc/udev/rules.d/90-usb-debug-ctrl.rules`:

```
ACTION=="add", SUBSYSTEM=="usb", ATTR{idVendor}=="1209", ATTR{idProduct}=="0001", TEST=="power/control", ATTR{power/control}="on"
```

then `sudo udevadm control --reload` and re-plug the board. With TLP also add the board to `USB_DENYLIST="1209:0001"`.
Update the PID with the FW-20 assignment (see "USB identity" above).

## Agent acceptance test

Procedure and result log: `firmware/docs/bringup.md`, section "Agent acceptance procedure" (FW-17).

## Licence

Everything under `firmware/` that is not third-party code (sources, build files and this documentation, including the
Markdown files) is licensed under the GNU General Public License v3.0 or later
(`SPDX-License-Identifier: GPL-3.0-or-later`, full text in [`LICENSE`](LICENSE)), Copyright (c) 2026 Martin Faltus (mafaltu).
Third-party code keeps its own licence: the submodules under `lib/` (TinyUSB: MIT; STM32C0 HAL driver: BSD-3-Clause;
CMSIS device C0: Apache-2.0; CMSIS 5: Apache-2.0) and the owned copies of ST files in `src/target/`
(`startup_stm32c071xx.s`, `system_stm32c0xx.c`: Apache-2.0; `stm32c0xx_hal_conf.h`: BSD-3-Clause), as stated in
each file's SPDX header.

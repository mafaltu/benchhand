<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (c) 2026 Martin Faltus (mafaltu) -->
# Firmware bring-up checklist (hardware-in-the-loop)

Owner bench checks, done with the ST-Link V3SET on the TC2050 pads (J102). One ordered list: work through the
sections in order; later sections assume the earlier ones passed. Lines stay unchecked until they have been run on
real hardware; write the result (date, value, scope screenshot name) after each line. Board #1 ran the first HIL session on 2026-11-14 (`hil-results-2026-11-14.md`); ticked lines carry its result.
SWD note: on board #1 SWD was unreliable at high clock (erase failed at 8 MHz, one read bit error at 1 MHz) and 100 kHz was
100 % reliable, so every SWD command here uses `freq=100`; the first connect attempt often fails, the second succeeds;
the CLI shows `Voltage 0.00V` (no VTref on the TC2050 pads, D-34) and works anyway.

Variables and conventions:

- `$STM32_PROGRAMMER_CLI` = `$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI`
  (the lines below also write it as `STM32_Programmer_CLI`).
- `<port>` = `/dev/serial/by-id/usb-mafaltu_BenchHand_<UID>-if00` (the PID 0x0001 is still a placeholder). Python checks
  need `pyserial`.
- `<sysfs>` = `/sys/bus/usb/devices/<bus>-<port>` of the control port (find it with
  `grep -l 1209 /sys/bus/usb/devices/*/idVendor`).
- Build, normal image (`firmware/build-arm/usb_debug_ctrl.elf` / `.hex` / `.bin`):
  `cmake -S firmware -B firmware/build-arm -DFW_TARGET=ON -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake && cmake --build firmware/build-arm`
- No-init RAM records (read with `-r32`): boot counter `{magic 0xB0075EED, ~magic, count}` at `0x20000000`, the
  count word at `0x20000008` (restarts at 1 after a power-on / brown-out reset); BOOTLOADER record
  `{magic 0xB007D0F0, ~magic, request counter}` at `0x2000000C` (request counter word at `0x20000014`). The ROM
  DFU loader uses SRAM from `0x20000000` (AN2606 Rev 70 Table 16), so after a DFU session `boot=` restarts at 1.
- Hardware-dependent results (measured current, USB enumeration names) are recorded in the lines below and then
  copied into `README.md`. DFU `:leave` (Q-2) is verified on board #1 and is the primary USB update path.

## 0. First power-up of a freshly assembled board (no firmware)

Board never powered, MCU blank. Goal: prove the assembly and that the silicon is alive before any firmware. Rules for
the whole section: control USB-C (J101) or TP101/TP103 only; nothing on the USB-B passthrough input J401, nothing on
USB-A J402, nothing on the relay terminals J201..J205 or the input header J301. Probe pads are on the BOTTOM side
(mirrored view; ground TP103, check TP109 against its net RLY5_DRV, ASSEMBLY_NOTES.md sec. 7). Net names and refdes
come from `design/netlist.json`; pin maps in `design/circuit.md` are partly stale, trust the netlist.

- [x] Orientation under a magnifier, part by part against the release assembly drawing (`output/manufacturing/
      USB-DEBUG-CTRL-r1_2643/assembly/`): pin-1 mark of U101 (LQFP-32), U104 (SOT-23A: pin 1 GND, 2 VOUT, 3 VIN), U401
      (VQFN-HR), U402 (UQFN-10), U403 (SC70-6), U102/D403/D404 (SOT-553), U311..U341 (ISOM8600); cathode bar of
      D201..D205 toward CONTROL_5V and of D311/D321/D331/D341 (1N4148W, D-59; the input clamps sit antiparallel
      to the ISOM8600 LED: cathode on IN<n>_A, i.e. pin 1, per the schematic); cathode bar of D401/D402 toward PASS_IN_5V /
      PASS_OUT_5V (SMF6.0A, unidirectional: confirm against `datasheets/SMF6.0A.pdf`); K mark of LED101, LED201..LED205
      (anode toward its resistor); Q201..Q205, Q401 (DMN2056U-7: gate pin 1, source 2, drain 3, D-40). D101 has no
      polarity. THT parts fully seated: K201..K205, J201..J205, J301, J401, J402, SW101..SW106. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] Solder bridges and tombstones: all 32 pins of U101 (0.8 mm pitch), every joint of U402 (area ratio 0.61, reflow
      may have starved or bridged it) and U401, U403, the SOT-553 parts, U311..U341, the 0603 arrays around U101 and
      R31x..R34x (60 two-terminal parts with unequal pad copper: tombstone risk; ASSEMBLY_NOTES.md sec. 2/4). - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] Parts vs BOM (`*_BOM.csv`): nothing missing. Intentionally empty: D405 (DNP), J102 (TC2050 pads only), TP101..TP109,
      TP201, TP301, TP401..TP407, MH1..MH4 (copper only; `*_DNP.csv`). - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] Unpowered resistance, DMM resistance mode, red (+) on the rail, black (-) on GND (TP103). Polarity matters: with red
      on GND the Q201..Q205 body diodes and flyback diodes D201..D205 conduct into the coils and give a false low
      reading. Pass criterion for each rail is "no short": reading above 1 kOhm and not falling toward 0. MLCCs make
      the reading creep up for seconds: wait for it to settle. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
  - [x] CONTROL_5V (TP101) to GND: expect high (hundreds of kOhm to open, no DC path to GND; inference from the netlist).
        Below 1 kOhm is a fail (check D101, C101, C102, U104 pin 3, the J101 VBUS pins). Then once with red on GND,
        black on TP101 in diode-test mode: record the reading (expect a forward drop of the Q20x body diode plus coil or
        flyback path, roughly 0.6..1.2 V, inference), a reading near 0 V is a fail. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
  - [x] CONTROL_3V3 (TP102) to GND: expect high (above 10 kOhm, MCU supply pins and pull-ups only); VBUS-to-3V3 check:
        TP101 to TP102 in both polarities must not read below 1 kOhm (an LDO pass-element diode drop one way is plausible). - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
  - [x] J101 shell to CONTROL_5V: open (the VBUS bridge under the shell is insulated by solder mask only, D-110;
        ASSEMBLY_NOTES.md sec. 3). Shell to GND: short (shell is GND). - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
  - [x] PASS_IN_5V (TP401) to GND: about 37 kOhm (R401+R402 127k, R406+R407 143k and R405+R415+R416 81.5k in parallel;
        computed, 1 % resistors). PASS_OUT_5V (TP402) to GND: about 3.2 kOhm (R409 4.7k parallel to R417+R418 10.3k).
        Both far from a short; a reading below 1 kOhm (IN) or below 2 kOhm (OUT) is a fail. TP401 to TP402 and TP401 to
        CONTROL_5V must not read below 100 kOhm (the passthrough domains never touch CONTROL_5V; `domain_check.out.txt`). - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
  - [x] Relay gates: RLY1_DRV..RLY5_DRV pads TP105..TP109 to GND: about 100 kOhm each (R201..R205 100R + R211..R215
        100k pull-down, MCU pin unpowered). Coil: CONTROL_5V (red) to the drain pin 3 of Q201..Q205 (black) = RLYn_COIL_N:
        160..196 Ohm at 23 C (HFD4/5 coil, `B_relay_driver.out.txt`); open or much higher means a missing flyback/coil joint. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
  - [x] Isolation: header J301 pins 1..8 (IN1_M, IN1_P, IN2_M, IN2_P, IN3_M, IN3_P, IN4_M, IN4_P) and relay terminals
        J201..J205 pins 1..3 (NC, COM, NO) to GND and to CONTROL_5V: open (above 10 MOhm; isolation barrier is the
        ISOM8600 and the relays). Relay released: COM to NC continuity (below 1 Ohm), COM to NO open. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] First power: lab supply set to 5.0 V with a 100 mA current limit, leads to TP101 (+) and TP103 (-) (or a USB-C
      breakout/cable plugged into J101 with a USB meter or a current-limited source; Rd 5.1k on CC1/CC2 is fitted).
      Raise from 0 V while watching the current. Expected idle current of a blank MCU: below 10 mA (design bound before
      enumeration 8.8 mA at the worst corner with LED and all pull-ups, `A_power_budget.out.txt`; a blank chip runs the
      ROM loader, inference). ABORT and remove power at once if: current above 30 mA or the limit is hit, any part gets
      too hot to hold a fingertip on (check U104, U101, D101, U401, Q201..Q205 after 30 s), smell or smoke. Record the
      current: ______ mA. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] Rails with a DMM (red on the pad, black on TP103): CONTROL_5V (TP101) = supply or USB voltage within about 20 mV
      (4.75..5.25 V from USB); CONTROL_3V3 (TP102) = 3.234..3.366 V (MCP1703A-3302, +-2 %, `A_ldo_dropout.out.txt`).
      Quiet pads, all expected below 0.1 V: TP105..TP109 (RLY drive), TP401, TP402, TP403, TP404, TP405, TP407 (nothing
      on USB-B, eFuse off: R404 pulls EN low). Expected about 3.3 V: NRST (TP104, internal pull-up) and the pull-up
      side of the buttons (R106..R111 pin 2). Expected about 5 V: RLYn_COIL_N at Q201..Q205 pin 3 (coil to 5 V, FET off). - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] LEDs on a blank MCU: LED101 (LED_USB, PA7) is OFF (driven only by firmware, pin is an input with pull-down after
      reset), and LED201..LED205 are OFF with no relay click (gates held low by R211..R215). Any lit LED is a fail. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [ ] Optional: the control USB-C connected to a PC shows a ROM-loader device (`lsusb`, expected an STMicroelectronics
      DFU device; not verified on hardware, `design/pinmap_checks.md` sec. 4 point 3). Record what appears; no result is a
      note, not a fail, if SWD works.
- [x] MCU alive over SWD without flashing, control port still powered, ST-Link V3SET on the J102 pads (SWDIO pad 2,
      SWCLK pad 9, NRST pad 3, GND pads 5/6). The pads carry no VTref (D-34): if the CLI reports target voltage 0 V or
      cannot connect, feed the programmer's VTref/VAPP input from TP102 (CONTROL_3V3) with a jumper wire and record that.
      `STM32_Programmer_CLI -c port=SWD freq=100 mode=UR` must connect and report `Device ID : 0x493` (STM32C071xx, RM0490 Table
      178 DEV_ID, p. 1009/1014) and flash size 64 KBytes (RM0490 sec. 31 FLASH_SIZE register: 0x040 = 64 KB). Record the
      printed device ID, revision and flash size. Do NOT flash anything yet. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] Factory option bytes: `STM32_Programmer_CLI -c port=SWD freq=100 mode=UR -ob displ`, save the output; `IWDG_STOP` expected 0x1
      (factory value, see section 2). Note nBOOT_SEL / nBOOT0 / nBOOT1 as printed. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- Not testable yet: the relays. RLYn gates are pulled low by R211..R215, so coil sanity and the contacts need firmware
  (or a forced gate) and are tested in the later sections; do not wire a gate to 3V3 or 5 V to force them.

## 1. Power-up safety (first, before anything is connected to the relays)

Do this on the blank board first (all pins are inputs with pull-downs, D-93/D-94), then once more after section 3.
Nothing may be wired to the relay contacts yet.

- [ ] Power-up glitch check (m-12): scope single-shot, trigger 1 V rising, on RLY1..RLY5 gates (RLYn_DRV pads
      TP105..TP109, probe ground TP201) and on LED_USB (PA7), over 10 power-ups of the control port: no pulse on any
      relay gate, no coil click. With nothing on the USB-B passthrough input LED_USB stays off as well. (With a
      source on USB-B, LED_USB and PASS_VBUS_EN/PASS_DATA_EN come on after the normal connect sequence - the
      passthrough defaults to ON after reset, FW-21 - that is expected, not a glitch.)
- [ ] Control port current on the very first power-up (USB meter, nothing on USB-B, no DUT): below the 500 mA
      budget, no hot part (touch / IR check of U401, the relays). Record the value.

## 2. Flashing, factory-state check, option byte IWDG_STOP = 0 (once per board)

The factory value `IWDG_STOP = 1` keeps the watchdog running in Stop, which would reset the board in every PC sleep;
the firmware reports `optr_err=1` and never enters Stop while it is 1 (FW-22 M-5). So the order is: flash, check the
factory state through the firmware, program the option byte, check again. Keep USB-B and any DUT disconnected.

- [ ] Flash the normal build (the only flash line; ST-Link on J102, control USB connected):
      `STM32_Programmer_CLI -c port=SWD freq=100 mode=UR -w firmware/build-arm/usb_debug_ctrl.hex -v -rst`
- [x] Enumerate (`dmesg` shows the CDC device, `ls /dev/serial/by-id/` shows the port), then
      `stty -F <port> raw -echo`, `cat <port> &` and `printf 'STATUS?\n' > <port>`: with the factory option byte it
      must read `optr_err=1`. Also `STM32_Programmer_CLI -c port=SWD freq=100 -ob displ` shows `IWDG_STOP` = 0x1. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] Program the option byte once: `STM32_Programmer_CLI -c port=SWD freq=100 mode=UR -ob IWDG_STOP=0` (setting it is part of
      the programming procedure, spec 4.5) - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] Read it back (m-11): `STM32_Programmer_CLI -c port=SWD freq=100 mode=UR -ob displ` shows `IWDG_STOP` = 0x0. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] After the option byte reload the board re-enumerates (re-plug the control USB if it does not): `STATUS?`
      reads `optr_err=0`. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)

## 3. Clocks

- [ ] Clock check (optional): SWD read `RCC_CFGR` (0x40021008) SWS = 000 (HSISYS), `RCC_CR` (0x40021000) HSIDIV = 000
      and HSIUSB48RDY (bit 23) = 1, `FLASH_ACR` (0x40022000) LATENCY = 1, `RCC_CCIPR2` (0x40021058) USBSEL = 0.

## 4. USB enumeration and protocol basics (USB CDC, TinyUSB 0.21.0, HSIUSB48 + CRS, ES0618 delay)

This image enumerates as a CDC ACM port (VID 0x1209, PID 0x0001).

- [x] Enumeration with HSIUSB48 + CRS: `dmesg` shows a new full-speed device and `cdc_acm` bound, no
      descriptor errors. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [ ] `lsusb -v -d 1209:0001` shows iManufacturer `mafaltu`, iProduct `BenchHand`, iSerial = 24 upper-case hex
      chars, iInterface `BenchHand control - send HELP`; bMaxPower 250mA, bmAttributes 0x80 (bus powered, no
      remote wakeup), bcdDevice 0.10.
- [ ] `ls /dev/serial/by-id/` shows `usb-mafaltu_BenchHand_<UID>-if00`; `<UID>` equals the `sn=` of `ID?`.
- [ ] `stty -F <port> raw -echo`, then `cat <port> &` and `printf 'ID?\n' > <port>`: one line
      `OK model=BenchHand hw=r1 fw=0.1.0 proto=1 sn=<UID>`.
- [ ] `printf 'STATUS?\n' > <port>`: `optr_err=0` (the option byte was programmed in section 2; the factory
      `optr_err=1` check is done there).
- [ ] `printf 'HELP\n' > <port>`: only `# ` lines, then `OK`; nothing lost or doubled.
- [ ] Echo guard (FW-23): `stty -F <port> sane` (cooked, echo on), `cat <port>` and send `HELP`: the output stops
      after the `OK`, no reply loop.
- [ ] DTR low (M-3): pyserial `serial.Serial(port, dsrdtr=False, rtscts=False)` with `dtr=False` set before
      `open()`, send `HELP\n` + `ID?\n`, read for 2 s; the bytes equal the DTR-high capture of the same commands.
- [ ] TX FIFO full / port closed mid-HELP: send `HELP` and close the port at once (no reader); wait 3 s, reopen,
      send `ID?`: exactly one `OK model=...` line (a rest of the dropped HELP may precede it only if the host
      buffered it; the board must answer `ID?` and must not hang). Repeat 5x; no IWDG reset (`reset=` in STATUS
      stays the same, `boot=` does not climb).
- [ ] Back-pressure: send a 4 KB burst of `ID?\n` lines without reading for 2 s, then read: replies come in order,
      board still responsive, no reset.
- [ ] ES0618 (no scope needed): 10 000 lines of `STATUS?` sent back to back without errors (`ERR` or garbled
      replies). The 800 ns delay itself is proven by the build (`_Static_assert`) and the disassembly (`movs r3, #4`
      before the delay loop in `dcd_int_handler`).
- [ ] Bus reset: `usbreset 1209:0001` (or `echo 0 > .../authorized; echo 1 > ...`) while a relay is ON: the relay
      drops, the port comes back, STATUS shows `ready=1` again after the reconfiguration.

## 5. Relays, buttons, inputs

(Spec section 7. Use a multimeter on the relay contacts NC/COM/NO and a 3.3 V / 12 V source for the inputs.)

- [x] Every relay 1..5: `SET <n> ON` -> `OK`, the relay clicks, COM-NO closes and COM-NC opens; `SET <n> OFF`
      releases; `STATUS?` `rly=` and the contact state agree. `SET ALL ON` / `SET ALL OFF` with the 10 ms stagger
      (no brown-out of the control port, no reset: `boot=` unchanged). - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [ ] MAX_ON: `SET 1 ON`, wait 60 s: relay 1 drops by itself and `STATUS?` shows it; `MAX_ON 1 5000` then `SET 1 ON`
      drops after 5 s; `PULSE 2 500` gives one 500 ms contact closure (scope or meter).
- [ ] Relay buttons 1..5 (XOR rule): with the relay requested OFF, holding the button closes it; with the
      relay requested ON by `SET`, holding the button opens it; release returns to the requested state. A button
      held at power-up stays blocked until released (FW-27). The press counters in `STATUS?` increase by one per press.
- [ ] USB button: held = passthrough disconnects (data, then VBUS); release = it returns to the requested state
      through the normal connect sequence; it never turns an OFF request ON (FW-05).
- [ ] Inputs IN1..IN4: 3.3 V and 12 V = HIGH, 0 V / open = LOW in `INPUT?`; edge counters count a 100 Hz square wave
      without missed edges.
- [ ] `WAIT <n> RISE 5000` returns `OK t=...` when the input rises and `ERR 5 timeout` when nothing happens.
- [ ] `SEQ SET 1 ON; DELAY 50; SET 2 ON; DELAY 100; SET 2 OFF; DELAY 50; SET 1 OFF` shows the expected timing on a
      scope (50 / 100 / 50 ms within a few ms); `SEQ ABORT` mid-run releases what the SEQ switched.

## 6. Passthrough (USB-B in -> USB-A out)

- [x] Connect: a USB source on USB-B (PC port or charger) and a device on USB-A: after the connect sequence
      (VBUS first, then data after PG OK and the settle time) `STATUS?` shows `usb=on`, LED_USB on, the device
      enumerates on the host behind it. With nothing on USB-B: `usb=no_input` (not a fault), VBUS_EN off, retried every
      2 s (FW-19). - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] Power only: `USB DATA OFF` -> the device stays powered but disconnects from data (host sees it leave);
      `USB DATA ON` -> re-enumerates without a power cycle. `USB DATA ON` never connects data while VBUS is off. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] `USB OFF` / `USB ON`, `USB DISCONNECT 2000`: USB-A output off for 2 s then back through the normal sequence;
      the device re-enumerates. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [ ] `ext` with a self-powered device: with the passthrough OFF and a self-powered device (own supply, e.g. a
      powered hub or a board with a battery) on USB-A, `STATUS?` shows `ext=1` and `usb=blocked` (external voltage on
      the output): the board never drives VBUS onto it (spec 4.2; read the `HELP USB` page for the exact states). `ext` returns to 0 when
      the device is unplugged.
- [ ] Overcurrent latch: with a load on USB-A (electronic load or resistor, above the eFuse limit), the eFuse trips,
      `STATUS?` shows a latched fault (`fault=efuse` or `output`, see `HELP STATUS`), the output stays off (latched, no automatic retry); after
      removing the load only `USB ON` rearms it, and it then reconnects. A DUT shorted from the start is retried
      every 2 s and the eFuse trips each time (FW-19).
- [ ] Upstream cable pulled while connected: latched fault (not `no_input`), rearm with `USB ON` once the source is
      back.
- [ ] Reset default: after power-up, `REBOOT` and an IWDG reset the passthrough comes back ON by itself with a
      source on USB-B (FW-21); relays never come back.

## 7. Suspend / resume (USB suspend -> Stop 0 -> USB wake, bus reset)

Precondition: `IWDG_STOP` = 0 (section 2); with `IWDG_STOP` = 1 the board never enters Stop and STATUS shows
`optr_err=1` (expected, not a failure of this section).

- [x] Suspend (Task 11 implements Stop; here Run with outputs off): `echo auto > .../power/control` with a short
      `autosuspend_delay_ms`, or PC sleep: outputs drop; after resume STATUS shows `suspends` incremented. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [ ] PC sleep (`systemctl suspend`) with relay 1 ON (`SET 1 ON`) and the passthrough ON: relay 1 and the
      passthrough go OFF (PASS_VBUS_EN / PASS_DATA_EN low), LED_USB off. Board current from the control port
      < 2.5 mA on a USB meter, if available. Record the value.
- [x] On PC wake: enumeration returns (`dmesg`), `STATUS?` shows `suspends=1`, `rly` all 0, `usb=on`, `lp_err=0`. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [ ] Runtime suspend x 20 (M-1): port closed, `echo auto > <sysfs>/power/control`,
      `echo 1000 > <sysfs>/power/autosuspend_delay_ms`, wait until `<sysfs>/power/runtime_status` = `suspended`;
      then open the port and send `STATUS?`: it answers every time, `suspends` +1 per cycle, `lp_err=0`. Record the
      final `suspends` value (expected 20 more than at the start).
- [ ] Bus reset without prior resume (spec 4.3 step 5, M-2): in runtime suspend, start `usbmon` capture
      (`modprobe usbmon`, Wireshark / `cat /sys/kernel/debug/usb/usbmon/<bus>u`), then as root
      `usbreset 1209:0001`. Expected: re-enumerates; `STATUS?` gives `ready=1 usb=on lp_err=0`; `SET 1 ON` -> `OK`
      (then `SET 1 OFF`). Record from the capture whether a resume preceded the reset; if the host always resumes
      first, record "not reproducible on this host" (logic covered by `test_app_bus_reset_during_suspend_recovers`).
- [ ] Re-plug during a pulse: `PULSE 1 30000`, unplug the control cable within the 30 s, re-plug: relay 1 does
      not come back on (`STATUS?` `rly` all 0).
- [ ] Autosuspend with the port closed (final fix wave, Cl-I1): on the owner's PC with its normal power settings
      and the README udev rule installed, send `SET 1 ON` and `USB ON`, close the port and wait 10 s; then
      `STATUS?`: `suspends` unchanged, `rly=1,...`, `usb=on` (board stays as commanded). Record whether the
      host had `power/control` = `auto` before the rule.
- [ ] Noise wake during PC sleep (final fix wave, Cl-m1): scope PASS_VBUS_EN (and PASS_DATA_EN) for 30 min of PC
      sleep: no pulse (a wake not confirmed within `APP_RESUME_CONFIRM_MS` = 10 ms keeps the passthrough off).
      After a real wake the passthrough comes back about 10 ms later than before this fix.
- [ ] Optional SWD read while suspended (`STM32_Programmer_CLI -c port=SWD freq=100 mode=HOTPLUG -r32 0x40005C40 4`,
      USB_CNTR): SUSPEN (bit 3) and SUSPRDY (bit 2) = 1. Note: an attached debugger can keep clocks running in
      Stop (DBGMCU); do the current measurement with the ST-Link disconnected.

## 8. Watchdog

- [ ] Watchdog check (m-12, ruling C7): build the debug variant
      `cmake -S firmware -B firmware/build-arm-stall -DFW_TARGET=ON -DFW_TEST_STALL_MS=5000 && cmake --build firmware/build-arm-stall`
      and flash `firmware/build-arm-stall/usb_debug_ctrl.hex`. After a power-up it runs 5 s normally, then stops
      refreshing the IWDG; after every IWDG reset it stalls at once, so the board resets about every 0.5 s.
      Read the counter twice 12 s apart:
      `STM32_Programmer_CLI -c port=SWD freq=100 mode=HOTPLUG -r32 0x20000008 4`
      Expected: the count grows by many (about 20 or more in 12 s, one per ~0.5 s IWDG period), not just 1-2.
      No relay clicks during the resets. Record both values.
- [ ] Re-flash the normal build afterwards (the stall build must never stay on a board).
- [ ] Debugger halt: with the normal image running, halt the CPU through the ST-Link (`STM32_Programmer_CLI
      -c port=SWD freq=100 mode=HOTPLUG -halt`, or a debugger break; check the command with `-h` on v2.23.0) with relay 1 ON and the passthrough ON: within about
      0.5 s the IWDG resets the board, relay 1 and the passthrough drop, and after the reset nothing but the
      passthrough comes back ON. (An attached debugger may freeze the IWDG while halted if DBGMCU allows it;
      record what was observed.)

## 9. REBOOT and BOOTLOADER (ROM USB DFU), DFU round trip

`$STM32_PROGRAMMER_CLI` = the CubeProgrammer path above.

- [ ] BOOTLOADER x5 (C-1, SFTRSTF+PINRSTF path): `cat <port> &`, `printf 'BOOTLOADER\n' > <port>`; `OK` is
      received (m-2), the CDC port disappears, then `dfu-util -l` lists `[0483:df11]` (STM32 BOOTLOADER).
      Leave DFU each time by a power cycle (re-plug the control USB). Record 5/5.
- [ ] m-2: the `OK` reply is printed by `cat <port>` before the detach in 5 of 5 tries.
- [ ] Outputs before the jump: with relay 1 ON (`SET 1 ON`) and the passthrough ON, send `BOOTLOADER`: relay 1
      and the passthrough go OFF before the CDC port disappears; in DFU no relay clicks and LED_USB stays off.
      Optional: SWD read of `GPIOA_ODR` / `GPIOB_ODR` in DFU shows RLY/VBUS_EN/DATA_EN low.
- [x] Q-2 `:leave`: `dfu-util -a 0 -s 0x08000000:leave -D firmware/build-arm/usb_debug_ctrl.bin`. Pass: within 5 s
      the board enumerates as `1209:0001` and `STATUS?` shows `rly=0,0,0,0,0 ... usb=on ... ready=1`.
      Record `reset=` and `boot=` (reset by the ROM -> `reset=sw`; a direct jump -> unchanged/other token).
      **Fallback** (not needed on board #1) if it does not come back: update without `:leave`, then a power cycle (re-plug the control USB)
      or `$STM32_PROGRAMMER_CLI -c port=SWD freq=100 mode=UR -rst`; record the chosen path here and in the README (Task 13). - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [ ] 10 power cycles of the control USB with no command: the board never enters DFU (`lsusb` shows `1209:0001`,
      never `0483:df11`). Record 10/10.
- [ ] Unplug during BOOTLOADER before detach: send `BOOTLOADER` and pull the control cable at once; after
      re-plug (power cycle) the normal application enumerates (`1209:0001`), not DFU. Repeat 3x.
- [x] REBOOT: `printf 'REBOOT\n' > <port>` -> `OK`, the port re-enumerates, `STATUS?` shows `reset=sw`, `boot=`
      +1, and the passthrough reconnects (`usb=on` with a source on USB-B). - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [ ] IWDG reset never enters DFU (optional, needs the m-12 stall build of Task 9): after a few IWDG resets
      `lsusb` never shows `0483:df11`.
- [ ] Note: during DFU the ROM also listens on USART1/2, I2C1/2 and SPI1/2 (AN2606 Table 16). BTN_RLY2 (PA3 =
      USART2_RX) is not pressed while in DFU; record whether a press there disturbs the DFU session (expected: no,
      the ROM needs a 0x7F sync byte).
- [ ] DFU round trip: `BOOTLOADER`, `dfu-util -a 0 -s 0x08000000 -D firmware/build-arm/usb_debug_ctrl.bin`
      (no `:leave`), power cycle: the application runs again and `ID?` answers; `STATUS?` `optr_err=0` still holds
      (the option byte survives). Record the `:leave` result of the Q-2 line above in `README.md` (Flashing).

## 10. Accepted risks from output/r1/open_risks.md (prototype tests)

- [ ] Item 1 / 2, USB 2.0 HS passthrough: a USB 2.0 high-speed stick through J401-U402-J402 enumerates at high speed
      (`lsusb -t` shows 480M behind the board, `dmesg` "high-speed USB device") and a copy-speed test reaches the
      stick's native speed (record MB/s direct vs through the board). If it falls back to full speed: re-place the
      U402/D404/J402 escape in r2.
- [ ] Item 7, current / thermal: 1.5 A load on USB-A for 10 min with the passthrough ON; the eFuse does not trip, the
      voltage drop at the load stays within spec, touch / IR check of U401, J401, J402 (record the temperatures and
      the ambient). Optional: 2 A step to see the trip.
- [ ] Item 3, SWD at full clock: connect the ST-Link at its maximum SWD frequency (`-c port=SWD freq=8000` and the
      highest the V3SET accepts); read and write flash (`-w ... -v`) and `-r32` repeatedly (20 times) with no
      connection error. Record the highest reliable frequency.

## 11. Agent acceptance procedure (FW-17, FW-24)

Purpose: prove that `HELP` alone is enough. Give a fresh agent only the README paste-in paragraph (with the wiring
filled in) and a wiring description; the agent gets no source code, no spec, no other documentation.

Setup: relay 1 = ESP32 BOOT (GPIO0) to GND; relay 2 = ESP32 EN to GND; input 1 = the ESP32 LED pin (3.3 V = HIGH);
passthrough = host on USB-B, ESP32 programmer on USB-A.

Task given to the agent (nothing else): "Put the ESP32 into boot mode and read input 1."

- [ ] The agent finds the port by the `/dev/serial/by-id/usb-mafaltu_BenchHand_*-if00` pattern, runs
      `stty -F <port> raw -echo` and sends `HELP` unprompted.
- [ ] It builds a correct boot-mode sequence from `HELP SEQ` (BOOT held, EN pulsed, BOOT released; e.g.
      `SEQ SET 1 ON; DELAY 100; SET 2 ON; DELAY 100; SET 2 OFF; DELAY 300; SET 1 OFF`) without help, checks the
      reply is `OK` and watches `seq=` in `STATUS?` until it is done.
- [ ] It reads input 1 with `INPUT?` (or `WAIT`) and reports the level; the ESP32 boot ROM message
      ("waiting for download") is seen on the ESP32 serial console as proof.
- [ ] It does not leave a relay ON at the end (or knows MAX_ON will release it).
- [ ] Record every point where the agent stumbled (a wrong command, an ERR it could not fix, a question to the owner
      that HELP should have answered, extra tries). **Every stumble is a HELP defect:** file it as a fix to
      `firmware/src/core/help_text.c` (with a `test_help` case), then repeat the test with a new agent. Result log:
      date, agent, number of commands sent, ERR count, defects found.

## Task 14: debug UART log (FW-32), optional

- [x] Build `-DFW_DEBUG_UART=ON` (dir `firmware/build-arm-dbg-on`), flash, ST-Link V3SET VCP open at 115200
      (`picocom -b 115200 /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_*-if0?`): banner line
      `BenchHand fw ... reset=... boot=... optr_err=... uid=...` and `usb init done` appear after power-up. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] Plug control USB: `usb bus_reset`, `usb configured`, then an `io` line with `led=1`. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] Press each button BTN1..BTN5 and BTN_USB: an `io` line with the matching `btn` bit (bounce may add lines). - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [x] Drive each isolated input IN1..IN4 HIGH: an `io` line with the matching `in` bit; relay command -> `rly` bit. - PASS 2026-11-14 board #1 (hil-results-2026-11-14.md)
- [ ] PC sleep (USB suspend) with IWDG_STOP=0: `usb suspend`, `enter stop`, `stop: woke from Stop 0, clock restore
      ok` + `wake` + `usb resume` on PC wake; text stays readable after wake (no baud shift).
- [ ] `REBOOT` / `BOOTLOADER` over CDC: `reboot: closing USB, reset` / `bootloader: ...` line fully printed before
      the reset.
- [ ] Re-flash the normal build (FW_DEBUG_UART OFF) before measuring the suspend current (< 2.5 mA), TC2050 cable
      unplugged.

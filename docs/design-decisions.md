<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
Copyright (c) 2026 Martin Faltus (mafaltu)

# BenchHand - design decisions

This is a builder-oriented **summary of the full private engineering log** of BenchHand r1. It keeps the decisions
you need to understand, build, modify or extend the board, the firmware and the enclosure. Hardware decisions carry
`D-xx` IDs, firmware decisions `FW-xx` IDs; the IDs match the references in the BOM and the other documents. Gaps in
the numbering are entries that were superseded or only concerned the internal workflow.

Each entry: **what** was decided - *why* - alternatives that were considered.

Related: [hardware overview](../hardware/docs/README.md), [known issues](../hardware/docs/known-issues.md),
[BOM](../hardware/bom/README.md).

## Architecture

### D-07 Buttons reach through the lid
**Six identical THT tactile switches with a ~13 mm actuator poke through the enclosure lid** - *no extra plunger
parts to design, print or lose* - low SMD switch plus printed plunger.

### D-09 All SMD on top, all THT soldered by hand
**Stencil + reflow for every SMD part on the top side, then all THT parts (connectors, relays, buttons, LEDs) by hand**
- *one-sided stencil, no paste-in-hole, easy home assembly* - paste-in-hole reflow of the THT parts.

### D-139 Name: BenchHand
**The project and USB product name is BenchHand; the r1 board silk keeps its working name "USB-DEBUG-CTRL r1"** - *the
board was already made when the name was chosen; the new name goes on the PCB from r2* - several other names, some
already taken.

### D-148 Copyleft licences
**Hardware CERN-OHL-S-2.0, firmware GPL-3.0-or-later, enclosure and docs CC BY-SA 4.0** - *open source, commercial
use allowed, modified versions must stay open; the name BenchHand is not for clones* - permissive licences;
non-commercial licences (not open source).

## USB control port

### Crystal-less USB
**The STM32C071 runs USB Full Speed from its internal HSIUSB48 oscillator, trimmed by CRS from the host's
Start-of-Frame packets** - *one part less, and the vendor-supported scheme for this MCU* - external crystal.

### D-79 USB-C receptacle GCT USB4105
**USB-C 2.0 receptacle with SMT signal pins and THT shell legs** - *the all-THT variant (USB4085) has holes too close
together for standard 2-layer fab rules; the shell legs keep the connector strong* - USB4085 with relaxed rules.

### D-39 Control-side refinements
**SOD-523 VBUS ESD clamp (D101, very low leakage), 2.2 uF VBUS bulk caps, 1.82 kOhm eFuse current-limit resistor,
test pads on the bottom, Same Sky TS02 switches** - *hand-solderable package, USB attach-charge limit, current limit
matched to the 1.5 A connectors, mechanical drawing available* - a tiny X1SON clamp, 4.7 uF bulk, a 2 A limit.

### D-33 Tag-Connect programming pads
**Programming and debug through Tag-Connect TC2050-IDC-NL pads (SWD + USART1 on PA9 / PA10, the ROM UART bootloader
pins) with a custom pad assignment** - *no connector cost, no header in the enclosure* - 1x5 pin header; the standard
ARM / Tag-Connect pinout. Pads 1, 8 and 10 carry no VTref / 3.3 V, so the programmer uses its own reference (D-34).

### D-118 No paste on the Tag-Connect pads
**The J102 pads get no stencil opening and no solder** - *the cable's spring pins press directly on flat, clean pads*
- paste the pads like any other.

### D-93 MCU pins chosen for routing
**Every movable GPIO was re-assigned to the side of the MCU that faces its circuit; USB, SWD, USART1 and supplies
stayed fixed** - *much shorter and cleaner routing on two layers* - keep the first pin map and route around it. Safety rule (D-94):
relay and enable outputs only on pins the ROM bootloader never drives high, so nothing switches on while programming.

## Relays

### D-18 THT relay Hongfa HFD4/5
**Five THT HFD4/5 signal relays (5 V coil, DPDT, gold-plated contacts), one pole used as NC / COM / NO** - *small,
gold-plated contacts for low-level signals, hand-solderable* - SMD version of the same relay.

### D-29 Prefer the 4.5 V coil variant
**HFD4/4.5 is the preferred coil when it can be bought; HFD4/5 is fitted otherwise, with the supply condition VBUS
>= 4.6 V at the board** - *a 5 V coil driven from USB VBUS has little pick-up margin when the coil is hot* - a
different, larger relay family. r1 accepts this residual risk (D-35).

### D-40 Lower-cost terminals, MOSFETs and USB-B
**Same Sky TB002-500-03BE terminals, DMN2056U-7 MOSFETs (specified at 2.5 V gate drive), Molex 67068-8000 USB-B** -
*about 4.5 EUR less per board* - BSS123-class MOSFETs (no guarantee below about 2.8 V gate drive),
the previous parts.

### D-58 Brighter LEDs
**Kingbright L-934LGD 3 mm green LEDs at about 1.4-2.5 mA (1.5 kOhm for coil LEDs, 1 kOhm for the USB LED); the USB LED
is off in suspend** - *cheaper and easy to source, chosen in a cost reduction round* - low-current LEDs at 0.3-0.5 mA.

### D-59 Diotec small-signal diodes and TVS
**Diotec 1N4148W for flyback and input clamp diodes, Diotec SMF6.0A for the passthrough TVS** - *reverse leakage was
checked against the input threshold budget, and the parts are easy to source* - Diodes Inc. 1N4148W, Littelfuse
SMF6.0A.

## Isolated inputs

### D-25 Input network 2 x 274 Ohm + 1.15 kOhm
**Two 274 Ohm 1 % 1206 series resistors (TCR <= 100 ppm/K) and a 1.15 kOhm shunt per channel** - *the only values found
that guarantee OFF at 1.0 V and ON at 3.0 V with all ISOM8600 and resistor tolerances stacked, and stay within 20 mA at
12 V; the margin is small, so test every channel* - the original 2 x 309 Ohm / 1.2 kOhm network (fails ON at 3.0 V
worst case).

### D-56 Keep the ISOM8600 opto-emulator
**Inputs use TI ISOM8600 rather than classic optocouplers** - *the guaranteed 0-1 V LOW / 3-12 V HIGH window was
derived for this part; re-deriving it was not worth the small saving* - conventional 4-pin optocoupler.

### D-48 J301 numbering from the Samtec drawing
**J301 pin numbers follow the Samtec TSW-RA drawing, and the nets are mapped so the upper row (viewed from outside) is
IN1+..IN4+ and the lower row IN1-..IN4-** - *the physical row order is what users wire to* - assume the generic
odd-row-on-top convention. IN1 is the leftmost column viewed from outside (D-77).

### D-60 Alternate input header
**The BOM lists an alternate J301 header (Samtec TSW-104-08-T-D-RA, matte tin, same footprint); any other 2x4
right-angle header needs a pin-1 and row check** - *cheaper or easier-to-find option* - Samtec gold flash part only.

## USB passthrough

### D-23 eFuse variant: latch-off
**TI TPS259474L eFuse (adjustable overvoltage lockout, power-good output, true reverse blocking, latch-off circuit
breaker), enabled through 10 kOhm with a 100 kOhm pull-down (default OFF)** - *a fault stays off until the host
re-arms it, and the variant was in stock* - TPS259470L (poor stock); auto-retry (`A`) variants.

### D-24 HS data switch TS3USB221E, default disconnected
**TS3USB221E analog switch on D+ / D-, its active-low enable driven through a MOSFET inverter with a pull-up, so data
is disconnected in reset or without 3.3 V** - *genuine USB 2.0 HS switch with powered-off isolation; the A version
was out of stock* - TS3USB221A.

### D-36 Passthrough rated 1.5 A
**1.5 A continuous; eFuse limit set to 1.61-2.01 A as short-circuit protection** - *no full-size USB-B receptacle
rated 2 A or more could be found at mainstream distributors* - 2 A or 3 A (would exceed the connector rating).

### D-44 Status through a Schmitt buffer
**Both passthrough status signals (eFuse power good and real output VBUS) go through an SN74LVC2G17 buffer with 5.5 V
tolerant inputs; the power-good threshold is taken from the input side** - *guaranteed logic levels, no 5 V on an MCU
pin even when the board is unpowered, an externally powered output never feeds an unpowered eFuse pin; accepted
limit: overvoltage plus a self-powered device can leave data connected* - Zener clamps; a comparator per signal.

### D-49 USB-A receptacle Molex 67643
**Molex 67643-0910 for the USB-A output** - *maintained part with a proper drawing, pin-1 marking and 3D model, rated
1.5 A per contact* - GCT USB1130 (not recommended for new designs); TE 292303.

### D-114 Short HS pair without length tuning
**The output D+ / D- pair keeps a short planar swap around a connector pin, without length-tuning meanders** - *the
whole pair is about 14 mm, roughly one critical length, so skew is far inside the budget; verified at 480 Mbit/s on
the prototype* - re-place the switch and connector for a fully coupled route.

## Power and suspend

### D-37 / D-145 USB suspend switches everything off
**In control-port USB suspend the relays are released, the passthrough is disconnected (data, then VBUS), the USB LED
is off and the MCU is in Stop; after resume the passthrough reconnects if it is still requested** - *a sleeping PC
behaves like a switched-off PC, and suspend current stays inside the USB budget* - keep the passthrough as commanded
during suspend (the original D-37). See also FW-06.

## PCB and fabrication

### D-10 Standard 2-layer board, no impedance control
**FR-4 1.6 mm, 1 oz, 2 layers; USB HS pairs calculated as about 90 Ohm coplanar pairs over ground, not impedance
controlled** - *cheapest standard prototype class* - impedance control or a thinner board.

### D-12 Isolation clearance at least 1.0 mm
**At least 1.0 mm copper clearance between each relay contact group, each input pair and everything else; 2 mm where
space allows** - *functional isolation for low-voltage bench use; the board is not for mains* - 0.5 mm; a fixed 2 mm
(would not fit).

### D-80 Connectors overhang the edge by ~2 mm
**Every edge connector's mating face sticks out about 2 mm past the PCB edge (screw terminals sit at the edge)** -
*the enclosure wall is 2 mm, so the faces end flush with the outside* - connectors flush with the PCB edge.

### D-54 Test pads are copper only
**Test pads and the Tag-Connect footprint are bare copper with no part and are not in the BOM; D405 stays a real
not-fitted option** - *nothing to buy or place* - mark them as not-fitted parts.

### D-110 VBUS bridge under the USB-C shell
**All four USB-C VBUS contacts are joined by a short top-layer track under the connector body, insulated by solder
mask** - *normal USB-C practice; there was no other legal path* - leave two VBUS contacts unconnected. Inspect the mask
before placing J101.

## Enclosure

### D-128 Two-part printed enclosure screwed from below
**Bottom shell with posts that lift the board, top shell with all cut-outs; 3 x 16 mm self-tapping screws from below
through the board into the lid posts, heads hidden under rubber feet** - *no inserts, no visible screws* - snap-fit;
screws from the top.

### D-144 Generic printable files first
**The primary enclosure deliverable is a printer-independent 3MF plus STEP; the slicer project for one specific
printer is only an example** - *anyone can print it on any printer* - publish the printer-specific project only.

## Firmware behaviour

### FW-05 Button behaviour
**A relay button inverts its relay only while it is held; the USB button forces the passthrough off while held and
never turns an OFF request on** - *a human can always override briefly, and the USB button only ever moves toward the
safe state* - XOR toggle also for the USB button.

### FW-06 / FW-08 Suspend = everything off
**In USB suspend: relays off, passthrough off, LED off, buttons ignored, MCU in Stop; only host resume wakes it** -
*matches a switched-off PC* - keep outputs as commanded; buttons active in suspend. Hardware side: D-37 / D-145.

### FW-13 Plain text protocol
**Text commands, exactly one `OK key=value ...` or `ERR <code> <text>` reply per command, no echo, no unsolicited
messages; `HELP` describes everything** - *easy for scripts and AI agents to use directly on the serial port* - JSON
lines; a PC-side library.

### FW-14 Relay ON-time limit
**Each relay switched on by the PC turns off after 60 s by default (`MAX_ON`, configurable, reset to 60 s on every
reboot)** - *a crashed script cannot leave a relay on forever* - 5 s; stored in flash.

### FW-18 USB DFU firmware update
**The `BOOTLOADER` command switches everything off and jumps to the STM32 ROM USB DFU bootloader on the USB-C port** -
*firmware updates without the programming cable* - SWD only.

### FW-19 No upstream power is not a fault
**With nothing on the USB-B input the passthrough reports `no_input` and retries every 2 s; a drop after a good
connection is a latched fault that needs `USB ON`** - *an unused passthrough must not look broken, a real fault still
needs an explicit re-arm* - latch everything.

### FW-21 Passthrough on after reset
**The USB passthrough defaults to ON after every reset; relays never restore** - *required by the original design brief;
relays must never switch by surprise* - passthrough OFF after reset.

<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
Copyright (c) 2026 Martin Faltus (mafaltu)

# BenchHand r1 - known issues and limitations

r1 is the first board revision (silkscreen "USB-DEBUG-CTRL r1", date code 2643). It works: on the first prototype
the power-up, USB serial, buttons, all five relays and their contacts, the USB passthrough with a High Speed device,
all four isolated inputs (including reverse polarity), firmware update over USB DFU and PC sleep / resume all passed.
This page lists what you should know before you build or use one. Items here are candidates for r2.

## Programming

### SWD is unreliable above about 100 kHz

On the prototype, SWD programming worked reliably at 100 kHz. At 8 MHz an erase failed, and at 1 MHz one bit was
read back wrong. The first connect attempt often fails and the second succeeds.

- **Workaround:** set your programmer to 100 kHz and verify after writing. Once firmware is on the board, later
  updates can go over USB DFU instead (firmware `BOOTLOADER` command), which does not use SWD.
- **Likely cause (not yet confirmed):** the Tag-Connect pads have no VTref pin, so the programmer sees 0 V target
  voltage and uses its own reference; the SWD traces also have a weaker return path than ideal. Feeding VTref from
  test pad TP102 (3.3 V) is the next thing to try.
- r2 will either add VTref to the programming footprint, or add a series resistor on SWCLK and better ground
  stitching, depending on what the measurement shows.

### Custom Tag-Connect pinout, no VTref

The TC2050 footprint is standard, so a generic TC2050-IDC-NL cable fits, but the pin assignment is custom, not the
standard ARM 10-pin SWD layout. **Tag-Connect cables made for a specific probe (for example TC2050-IDC-NL-050-STDC14
for ST-LINK V3) do not work.** Wire the generic cable to your probe as in the
[hardware overview](README.md#adapter-wiring).

## Isolation

### 1 mm clearance, functional isolation only - not for mains

The relay contact groups and the four input pairs are separated from everything else by at least 1.0 mm of copper
clearance (about 1.49 mm for the relays, about 1.04 mm for the inputs). The 2 mm target was not reached anywhere.
This is **functional isolation for low-voltage bench use**, with no safety rating.

- Do not switch mains or any hazardous voltage with the relays.
- Do not connect mains-referenced signals to the inputs.
- Relay contacts are rated 1 A at up to 30 V DC, resistive.

## USB passthrough

### High Speed pair is not impedance controlled

The board was made without impedance control. The D+ / D- pair between the data switch and the USB-A connector is
not coupled over part of its length (D+ and D- had to swap order around a connector pin) and has about 5.5 mm of
length difference (about 33 ps). The whole pair is short (about 14 mm), which is why this is expected to be fine.

On the prototype a USB 2.0 High Speed flash drive enumerated at 480 Mbit/s through the passthrough and survived
repeated on / off / data-only switching. No eye diagram or long transfer stress test has been done. If you see
High Speed problems with a particular device, try a shorter or better cable first, and please report it.

### Overvoltage with a self-powered device

If the upstream source rises above the eFuse overvoltage threshold (about 5.5-5.8 V) while a self-powered device on
the USB-A side keeps its VBUS up, the status signals can still read "OK" and the data lines can stay connected. The
eFuse still blocks the overvoltage, so the device is not harmed.

## Assembly

### U402 fine-pitch pads and paste release

U402 (TS3USB221E, UQFN-10, 0.5 mm pitch, pads 0.2 x 0.55 mm) has a stencil area ratio of about 0.61 with a 0.12 mm
stencil, below the usual 0.66 guideline. Paste may not release fully. Inspect the paste print before placing and the
joints after reflow under magnification, and touch up if needed. A slightly enlarged aperture or a thinner stencil
locally would help.

### Only solder mask under the USB-C connector

A thin VBUS track runs under the metal shell of the USB-C connector J101, insulated only by solder mask. A scratched
or missing mask there would short VBUS to ground through the shell.

- Inspect the mask under magnification before placing J101; cover the area with a small piece of Kapton tape if in
  doubt.
- After soldering, check that the shell (GND) is not shorted to `CONTROL_5V` (TP101).

### Silkscreen is secondary

Several references are hidden or crowded on the silkscreen. Place parts from the assembly drawing
(`benchhand-r1-assembly-top.pdf`), which shows every reference inside its part outline. Details are in the
[assembly notes](../fabrication/benchhand-r1-assembly-notes.md).

## Relays

### Hot-coil pick-up needs a healthy USB port

The HFD4/5 has a 5 V coil. With a hot coil and VBUS near the low end of the USB range, pick-up is not guaranteed.
Use a USB port that keeps VBUS at or above about 4.6 V at the board (a powered hub or a PC port, not a long thin
cable). The HFD4/4.5 variant gives more margin and fits the same footprint.

## Not measured yet

These items are designed and calculated, but have not been measured on hardware:

- **Suspend current** of the whole board in USB suspend (calculated about 1.7 mA against a 2.5 mA budget).
- **Glitches at power-up**: relay gates and passthrough enables during plug-in, reset and programming (no scope
  capture yet).
- **1.5 A passthrough for 10 minutes**: temperature of the eFuse U401 and the USB connectors.
- **SWD at full clock** with VTref fed from TP102 (see above).
- **Isolated input thresholds** at 3.0 V (ON) and 12 V (upper limit); the inputs were tested at 5 V only. The
  worst-case threshold window is narrow on paper.
- **Relay hot-coil pick-up** at reduced VBUS.
- **Isolation voltage** between domains.
- **Connector overhang vs. the enclosure wall** (about 2 mm) was designed from drawings and renders.

## Craftsmanship

These things don't change how the board works, but an experienced human designer would have done them differently.

### The layout is not polished

The board passes DRC and electrical checks and it works. A human eye will still find odd details: a few tracks that
take a strange route, copper pours with peculiar shapes (sharp slivers and small arcs, for example around the
DIGITAL IN section), and places that could be tidier. A design tool doesn't care about these, but people do. Cleaning
them up is a good first contribution for r2.

### The schematic is correct, not pretty

The schematic was generated from the netlist. It is electrically correct (ERC clean, matches the PCB), but its
placement and readability are not always what a human would draw. Some sheets are harder to follow than they need to
be. Use the [hardware overview](README.md) together with the schematic PDF.

## More to come

The owner is still assembling and testing r1 boards. Findings from assembly and further testing will be added here.

- _(placeholder)_
- _(placeholder)_
- _(placeholder)_

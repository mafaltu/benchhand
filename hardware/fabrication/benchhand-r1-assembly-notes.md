<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
Copyright (c) 2026 Martin Faltus (mafaltu)

# BenchHand r1 - assembly notes

Home assembly of BenchHand r1 (board silkscreen: **USB-DEBUG-CTRL r1**, date code 2643): stencil 0.12 mm + reflow
for all SMD parts (all on TOP), then all THT parts hand-soldered. No paste-in-hole.

Board: 188.8 x 40 mm, 2 layers, FR-4 1.6 mm, 1 oz copper. The prototype was made with lead-free HASL and green mask;
any finish and mask colour works.

Files in this directory:

| File | Content |
|---|---|
| `benchhand-r1-gerber.zip` | Gerbers + PTH / NPTH drill files |
| `benchhand-r1-pnp.csv` | home-assembly placement file (KiCad rotations, origin top-left; not an assembler CPL) |
| `benchhand-r1-dnp.csv` | positions that are not fitted |
| `benchhand-r1-assembly-top.pdf`, `benchhand-r1-assembly-bottom.pdf` | assembly drawings TOP / BOTTOM |
| `benchhand-r1-pcb.pdf` | PCB drawing (PDF) |

The bill of materials (manufacturer + MPN, key spec, allowed substitutes, optional distributor codes) is in
[`../bom/`](../bom/).

## 1. Placement authority: the assembly drawing

- **Place every part from the assembly drawing, not from the silkscreen.** The drawing shows the reference inside
  every part outline (the bottom drawing shows the test pads).
- 16 TOP silkscreen references are intentionally hidden because there was no unambiguous spot for them: U102, R101,
  U402, U403, D404, R315, R325, R335, R345, R405, R323, R333, J101, R334, R406, R415.
- Labels that sit almost as close to a neighbour as to their own part: R202 / R203 / R205 next to the R22x resistors
  (100 Ohm vs 1.5 kOhm) - check the value against the drawing. D403 text sits 0.13 mm from its pin-1 triangle. J401 /
  J402 labels end up under the connector bodies after assembly.
- Polarity: LEDs by the K mark, diodes by the cathode bar, ICs by the pin-1 mark (U101 LQFP-32, U401, U402, U403,
  U102 / D403 / D404 SOT-553, ISOM8600 U311..U341).

## 2. Paste and stencil

- Stencil 0.12 mm, TOP only (the bottom has no paste openings).
- **U402 (TS3USB221E, UQFN-10, pads 0.2 x 0.55 mm): area ratio 0.61 < 0.66.** Inspect the paste print on U402 before
  placing; reprint if pads are starved or bridged. After reflow inspect every U402 joint under magnification and touch
  up if needed. U401 / U402 use the TI recommended land patterns.
- J102 (Tag-Connect TC2050-IDC-NL) is a pad-only footprint: **no paste, no part** (the cable's spring pins press on
  the pads). The stencil has no openings there; keep it clean of paste.
- J101 (USB-C, GCT USB4105-GF-A): the SMD signal pins get paste; the THT shell legs have no paste (hand-soldered).

## 3. J101 - inspect the mask before placement

- A VBUS bridge track (A9/B4 -> A4/B9, 0.25 mm) runs under the metal shell of J101 and is insulated by solder mask
  only. **Before placing J101, inspect the mask over the bridge under magnification**: no scratch, no peeling, no
  exposed copper. If in doubt, cover it with a small piece of Kapton tape.
- After soldering, measure shell (GND) to CONTROL_5V (TP101): must be open (no short).

## 4. Reflow

- Lead-free HASL works with both leaded and lead-free solder paste; use the reflow profile of the paste you printed.
- Use a profile with a **preheat soak** (e.g. 150-180 C for 60-90 s) before peak: 60 two-terminal parts have unequal
  copper on their two pads, and a soak reduces tombstoning.
- After reflow: visual inspection of U101, U401, U402, U403, U102, D403, D404, the 0603 arrays around U101 and the
  isolated-input block (R31x..R34x, U311..U341). Check for bridges and tombstones.

## 5. Hand-soldered THT after reflow

Order (low to high):

1. LEDs LED101, LED201..LED205 (K mark). If the LEDs must reach through an enclosure lid, set their height before
   soldering (see the enclosure documentation).
2. J301 2x4 right-angle header (pins pointing outward past the lower edge).
3. Relays K201..K205 (HFD4/5).
4. Terminal blocks J201..J205 (wire entry toward the board edge).
5. USB-B J401, USB-A J402.
6. Tactile switches SW101..SW106.
7. J101 shell legs.

Connector mating faces overhang the board edge by about 2 mm on purpose (for the enclosure wall). Seat them fully on
the board; do not align them with the edge.

## 6. Not fitted and copper-only footprints

- **D405: do not populate** (optional Schottky clamp on PASS_OUT_5V; fit only if a measurement shows the output going
  below -0.8 V).
- J102 (TC2050 pads), TP101..TP109, TP201, TP301, TP401..TP407 (bottom test pads), MH1..MH4 (M3 holes): copper only,
  no part, no solder.

## 7. Bottom test pads

The 18 test pads are on the BOTTOM, each label beside its own pad. Read the bottom view mirrored. TP109 has the
weakest label (it sits above the TP105 pad, level with its own pad): check TP109 against the table (net RLY5_DRV)
before probing.

| Pad | Net | Pad | Net |
|---|---|---|---|
| TP101 | CONTROL_5V | TP201 | GND |
| TP102 | CONTROL_3V3 (also the 3.3 V reference for an SWD programmer) | TP301 | GND |
| TP103 | GND | TP401 | PASS_IN_5V |
| TP104 | NRST | TP402 | PASS_OUT_5V |
| TP105 | RLY1_DRV | TP403 | PASS_OUT_OK |
| TP106 | RLY2_DRV | TP404 | PASS_VBUS_EN |
| TP107 | RLY3_DRV | TP405 | PASS_DATA_EN |
| TP108 | RLY4_DRV | TP406 | GND |
| TP109 | RLY5_DRV | TP407 | PASS_PG_OK |

## 8. First power-up

1. Before power: check CONTROL_5V, CONTROL_3V3, PASS_IN_5V and PASS_OUT_5V to GND for shorts.
2. Apply a current-limited 5 V / 100 mA supply to J101 first; check CONTROL_3V3 at TP102 (3.3 V).
3. Program over the TC2050 pads (J102: 2 SWDIO, 9 SWCLK, 3 NRST, 5 / 6 GND, **no VTref**; the programmer needs its own
   reference or 3.3 V from TP102). A generic TC2050-IDC-NL cable fits, but the pinout is custom: probe-specific
   cables (e.g. the ST-LINK STDC14 one) do not work, see [adapter wiring](../docs/README.md#adapter-wiring). Use **100 kHz SWD clock** on r1. See the firmware README for building and flashing.
4. Then test: USB enumeration on the USB-C port, each relay by command and by button, the isolated inputs, and the
   USB passthrough with a USB 2.0 High Speed device (it should enumerate at 480 Mbit/s).

Known r1 issues and untested items: [../docs/known-issues.md](../docs/known-issues.md).

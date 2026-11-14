<!-- SPDX-License-Identifier: CERN-OHL-S-2.0 -->
Copyright (c) 2026 Martin Faltus (mafaltu)

# BenchHand r1 - bill of materials

Distributor-neutral BOM of BenchHand r1 (board silkscreen: **USB-DEBUG-CTRL r1**, date code 2643).

- Machine-readable file: [`benchhand_r1_bom.csv`](benchhand_r1_bom.csv) (comma separated, UTF-8, one row per
  distinct part). The **Key spec** column is the substitution criterion; the **Substitution** column says whether a
  part may be replaced.
- 141 fitted parts in 43 distinct part numbers per board, plus 4 rows of not-fitted positions (Qty 0).
- Refs, quantities and MPNs are cross-checked against the released r1 BOM and DNP list by
  `tools/public/check_public_bom.py` (0 differences required).
- `D-xx` references in the notes are decision IDs of the r1 design log.

## Distributor codes: snapshot only

The Mouser / DigiKey / TME / LCSC columns are a **convenience snapshot collected 2026-10-20 .. 2026-10-28**, matched
by exact MPN. They are not a recommendation of any distributor or assembler. **Verify every code, the exact MPN and
the packaging (cut tape / reel) on the distributor page before ordering.** An empty cell means no code was verified
for that exact MPN, not that the part is unavailable there. No prices or stock levels are given because they go
stale.

## Substitution rules

1. **"any part meeting key spec"** (resistors, capacitors, 1N4148W, SMF6.0A TVS, 3 mm LEDs): any manufacturer is
   fine if every item of the key spec is met or exceeded: value, tolerance, voltage rating, dielectric (X7R or better,
   C0G is always acceptable), power rating, package size. Do not change values: several resistors set thresholds
   (eFuse current limit, OVLO, power-good, isolated input thresholds), see the Notes column.
2. **"alternate: ..."**: the listed alternate MPN fits the same footprint and may be used as-is.
3. **"no substitute without footprint/datasheet check: ..."**: the board, the firmware or a design calculation
   depends on this exact part. A replacement needs a check of the land pattern, pinout, height and the electrical
   parameters named in the reason. In particular:
   - **U401 TPS259474LRPWR**: latch-off + power-good variant; the auto-retry (`A`) and other TPS25947x variants
     behave differently.
   - **U402 TS3USB221ERSER**, **U102 / D403 / D404 TPD2E2U06DRLR**: USB 2.0 high-speed data path.
   - **U101 STM32C071K8T6**: the firmware is built for this MCU.
   - **U311 .. U341 ISOM8600DFGR**: the input resistor network is sized for this opto-emulator.
   - **K201 .. K205 Hongfa HFD4/5**: coil drive and pick-up margin are analysed for the HFD4 family; HFD4/4.5
     (4.5 V coil) gives more pick-up margin and fits the same footprint. With HFD4/5 keep VBUS >= 4.6 V at the board.
   - **J101 / J401 / J402 USB connectors, J201 .. J205 terminals, J301 header, SW101 .. SW106 buttons**: footprint,
     mating side and height (buttons and connectors must line up with the enclosure).
   - **Q201 .. Q205, Q401 DMN2056U-7**: logic-level gate at 3.3 V and SOT-23 pinout G=1 S=2 D=3.

## Assembly summary

Reference build is home assembly (full notes: [assembly notes](../fabrication/benchhand-r1-assembly-notes.md)):

1. Stencil 0.12 mm, **all SMD parts on TOP**, solder paste + reflow. The bottom side has no SMD parts.
2. Inspect U402 (UQFN-10, fine pitch) paste print and joints; inspect the solder mask over the VBUS bridge under J101
   before placing J101.
3. After reflow, **hand-solder all THT parts** (no paste-in-hole): J101 shell legs, USB-B J401, USB-A J402,
   terminals J201 .. J205, header J301, relays K201 .. K205, buttons SW101 .. SW106, LEDs LED101 / LED201 .. LED205.
4. Not fitted: D405 (DNP, optional clamp), J102 (Tag-Connect TC2050-IDC-NL pads, no part, no paste), test pads TP*,
   mounting holes MH1 .. MH4.

Place parts from the assembly drawing, not from the silkscreen; polarity: LEDs by the K mark, diodes by the cathode
bar, ICs by the pin-1 mark.

## BOM table

Subst.: `any` = any part meeting the key spec, `alt` = listed alternate, `check` = no substitute without a
footprint / datasheet check (reason in the CSV).

### ICs

| Refs | Qty | Value | Key spec | Manufacturer MPN | Fitting | Subst. |
|---|---|---|---|---|---|---|
| U101 | 1 | STM32C071K8T6 | STM32C071K8T6, LQFP-32 7x7 mm 0.8 mm pitch | STMicroelectronics STM32C071K8T6 | SMD top | check |
| U104 | 1 | 3.3 V LDO | 3.3 V fixed LDO, >= 250 mA, input rating >= 16 V, SOT-23-3 pinout as MCP1703A SOT-23A | Microchip MCP1703AT-3302E/CB | SMD top | check |
| U401 | 1 | TPS259474L | TPS259474L variant (adjustable OVLO + PG, latch-off), VQFN-HR-10 2x2 mm (RPW) | Texas Instruments TPS259474LRPWR | SMD top | check |
| U402 | 1 | TS3USB221E | TS3USB221E, VCC 2.3-3.6 V, 1 GHz bandwidth, UQFN-10 2x1.5 mm 0.5 mm pitch (RSE) | Texas Instruments TS3USB221ERSER | SMD top | check |
| U403 | 1 | SN74LVC2G17 | non-inverting dual Schmitt buffer, 5.5 V tolerant inputs, Ioff partial power-down, SC70-6 (DCK) | Texas Instruments SN74LVC2G17DCKR | SMD top | check |
| U311 U321 U331 U341 | 4 | ISOM8600 | ISOM8600DFGR, 4-pin SOIC (DFG) | Texas Instruments ISOM8600DFGR | SMD top | check |
| D403 D404 U102 | 3 | TPD2E2U06 | 2-channel ESD, VRWM 5.5 V, IO capacitance 1.5 pF typ, 5-pin SOT (DRL) | Texas Instruments TPD2E2U06DRLR | SMD top | check |

### Discretes

| Refs | Qty | Value | Key spec | Manufacturer MPN | Fitting | Subst. |
|---|---|---|---|---|---|---|
| D101 | 1 | TPD1E10B06 | 1-channel bidirectional ESD TVS, VRWM 5.5 V, SOD-523 | Texas Instruments TPD1E10B06DYAR | SMD top | check |
| Q201 Q202 Q203 Q204 Q205 Q401 | 6 | DMN2056U | N-MOSFET 20 V, RDS(on) <= 45 mOhm @ VGS 2.5 V, VGS(th) <= 1.0 V, SOT-23 G=1 S=2 D=3 | Diodes Incorporated DMN2056U-7 | SMD top | check |
| D201 D202 D203 D204 D205 D311 D321 D331 D341 | 9 | 1N4148W | 1N4148W small-signal diode, IR < 25 nA @ 20 V 25 C, VF < 1 V @ 50 mA, SOD-123F or SOD-123 | Diotec Semiconductor 1N4148W | SMD top | any |
| D401 D402 | 2 | SMF6.0A | unidirectional TVS, VRWM 6.0 V, VBR 6.6-7.4 V @ 10 mA, VC 10.3 V, SOD-123FL | Diotec Semiconductor SMF6.0A | SMD top | any |

### Connectors

| Refs | Qty | Value | Key spec | Manufacturer MPN | Fitting | Subst. |
|---|---|---|---|---|---|---|
| J101 | 1 | USB-C 2.0 | USB-C 2.0 receptacle 16-pin, top mount horizontal, SMT signal pins + THT shell legs | GCT USB4105-GF-A | SMD top + hand-solder (shell legs) | check |
| J401 | 1 | USB-B 2.0 | full-size USB-B 2.0 receptacle, THT right angle, >= 1.5 A | Molex 0670688000 | THT (hand-solder after reflow) | check |
| J402 | 1 | USB-A 2.0 | USB-A 2.0 receptacle, THT right angle, Molex 67643 footprint | Molex 67643-0910 | THT (hand-solder after reflow) | check |
| J201 J202 J203 J204 J205 | 5 | 3-pos 5.0 mm | 3-position PCB screw terminal, 5.0 mm pitch, 1.30 mm holes, wire entry toward the board edge, 15 A / 300 V (D-40) | Same Sky TB002-500-03BE | THT (hand-solder after reflow) | check |
| J301 | 1 | 2x4 RA header | 2x4 male header 2.54 mm, right angle, Samtec lead style -08 | Samtec TSW-104-08-F-D-RA | THT (hand-solder after reflow) | alt |

### Electromechanical

| Refs | Qty | Value | Key spec | Manufacturer MPN | Fitting | Subst. |
|---|---|---|---|---|---|---|
| K201 K202 K203 K204 K205 | 5 | HFD4/5 | Hongfa HFD4/5: 5 V coil, DPDT, THT | Hongfa HFD4/5 | THT (hand-solder after reflow) | check |
| SW101 SW102 SW103 SW104 SW105 SW106 | 6 | tact 6x6 | tact switch 6x6 mm THT, actuator 13.0 mm above PCB, 160 gf | Same Sky TS02-66-130-BK-160-LCR-D | THT (hand-solder after reflow) | check |
| LED101 LED201 LED202 LED203 LED204 LED205 | 6 | green 3 mm | 3 mm green diffused THT LED, VF typ 1.9 V / max 2.2 V @ 2 mA | Kingbright L-934LGD | THT (hand-solder after reflow) | any |

### Capacitors

| Refs | Qty | Value | Key spec | Manufacturer MPN | Fitting | Subst. |
|---|---|---|---|---|---|---|
| C101 C102 | 2 | 2.2 uF | 2.2 uF 16 V X7R 0805 +-10 % | Yageo CC0805KKX7R7BB225 | SMD top | any |
| C103 | 1 | 4.7 uF | 4.7 uF 16 V X7R 0805 +-10 % | Samsung Electro-Mechanics CL21B475KOFNNNE | SMD top | any |
| C401 C405 | 2 | 1 uF | 1 uF 25 V X7R 0805 +-10 % | Samsung Electro-Mechanics CL21B105KAFNNNE | SMD top | any |
| C104 C105 C402 C406 C407 C408 | 6 | 100 nF | 100 nF 50 V X7R 0603 +-10 % | Yageo CC0603KRX7R9BB104 | SMD top | any |
| C106 C107 C108 C109 C110 C111 C311 C321 C331 C341 | 10 | 10 nF | 10 nF 50 V X7R 0603 +-10 % | Yageo CC0603KRX7R9BB103 | SMD top | any |
| C403 | 1 | 3.9 nF | 3.9 nF 50 V X7R or C0G 0603 +-10 % | Yageo CC0603KRX7R9BB392 | SMD top | any |
| C404 | 1 | 2.2 nF | 2.2 nF 50 V X7R 0603 +-10 % | Samsung Electro-Mechanics CL10B222KB8NNNC | SMD top | any |

### Resistors

| Refs | Qty | Value | Key spec | Manufacturer MPN | Fitting | Subst. |
|---|---|---|---|---|---|---|
| R201 R202 R203 R204 R205 | 5 | 100 Ohm | 100 Ohm 1 % 0603 >= 0.1 W | Yageo RC0603FR-07100RL | SMD top | any |
| R410 R411 | 2 | 49.9 Ohm | 49.9 Ohm 1 % 0603 >= 0.1 W | Yageo RC0603FR-0749R9L | SMD top | any |
| R112 R315 R325 R335 R345 R413 | 6 | 1 kOhm | 1 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-071KL | SMD top | any |
| R313 R323 R333 R343 | 4 | 1.15 kOhm | 1.15 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-071K15L | SMD top | any |
| R221 R222 R223 R224 R225 R415 | 6 | 1.5 kOhm | 1.5 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-071K5L | SMD top | any |
| R408 | 1 | 1.82 kOhm | 1.82 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-071K82L | SMD top | any |
| R409 R417 | 2 | 4.7 kOhm | 4.7 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-074K7L | SMD top | any |
| R101 R102 | 2 | 5.1 kOhm | 5.1 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-075K1L | SMD top | any |
| R418 | 1 | 5.6 kOhm | 5.6 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-075K6L | SMD top | any |
| R403 | 1 | 10 kOhm | 10 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-0710KL | SMD top | any |
| R314 R324 R334 R344 | 4 | 22 kOhm | 22 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-0722KL | SMD top | any |
| R402 | 1 | 27 kOhm | 27 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-0727KL | SMD top | any |
| R405 | 1 | 33 kOhm | 33 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-0733KL | SMD top | any |
| R407 | 1 | 43 kOhm | 43 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-0743KL | SMD top | any |
| R416 | 1 | 47 kOhm | 47 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-0747KL | SMD top | any |
| R106 R107 R108 R109 R110 R111 R211 R212 R213 R214 R215 R401 R404 R406 R412 R414 | 16 | 100 kOhm | 100 kOhm 1 % 0603 >= 0.1 W | Yageo RC0603FR-07100KL | SMD top | any |
| R311 R312 R321 R322 R331 R332 R341 R342 | 8 | 274 Ohm | 274 Ohm 1 % 1206 >= 0.25 W, TCR <= 100 ppm/K | Yageo RC1206FR-07274RL | SMD top | any |

### Not fitted

| Refs | Qty | Value | Key spec | Manufacturer MPN | Fitting | Subst. |
|---|---|---|---|---|---|---|
| D405 | 0 | Schottky (DNP) | Schottky diode, SOD-123; no part chosen | - | DNP | - |
| J102 | 0 | Tag-Connect pads | copper pads only (10 pads 1.27 mm + 3 NPTH) | - | not populated (copper pads only) | - |
| TP101 TP102 TP103 TP104 TP105 TP106 TP107 TP108 TP109 TP201 TP301 TP401 TP402 TP403 TP404 TP405 TP406 TP407 | 0 | test pad | copper pad only | - | not populated (copper pads only) | - |
| MH1 MH2 MH3 MH4 | 0 | mounting hole | NPTH, no part | - | not populated (board feature) | - |

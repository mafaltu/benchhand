# Design notes - USB-DEBUG-CTRL r1 enclosure (pilot)

Board: USB-DEBUG-CTRL r1, date code 2643, `cad/usb_debug_ctrl.kicad_pcb` sha256 8e5fd47b35cd…,
STEP `output/manufacturing/USB-DEBUG-CTRL-r1_2643/USB-DEBUG-CTRL-r1_2643_3D.step` sha256 80cd22ca6c20….
Decisions D-127..D-138, D-141..D-144. Model frame and rebuild: README.md. Machine-readable results: `out/checks.json`.

## Construction

- **Bottom** (black): floor + walls up to the PCB top surface, no connector cut-outs except a 0.46 mm relief under
  the USB-B shell flange (the shell dips below the PCB top beyond the board edge). 4 corner posts (d9 → d7 at the
  PCB, inside the 7 mm keep-out) + 11 floor pillars (d2.4-4) under the 6 buttons and behind the 5 terminal screw
  rows, auto-placed ≥ 0.5 mm from every bottom-side pad / THT pad / via. Screw head pockets d6.6 from below,
  covered by adhesive feet in d10.5 x 0.5 recesses.
- **Lid** (black + white inlay): all openings; connector notches open towards the seam so the lid drops straight
  over the board; conical 45° finger dimples d11 around the button stems; LED holes d3.2; one 4.5 mm slot per
  terminal block over the three screws; terminal wire windows in the front wall with 1.2 mm gussets behind the
  2.5 mm wall fingers between them; input-header opening sized for a Dupont 2x4 housing; plug-overmold pocket around USB-C
  (its face sits 0.6 mm behind the outer wall); 4 posts d7 with pilot holes and ribs to the walls.
- **Seam**: rabbet - lid skirt (0.9 x 1.2 mm) outside, bottom tongue inside, 0.15 mm side fit, 0.2 mm vertical
  gaps; the vertical stack is set only by the posts (bottom post top = PCB bottom, lid post bottom = PCB top).
- **Fasteners**: 4 x self-tapping 3 x 16 pan head (head d6.0 x 2.4 assumed until the purchased part is measured),
  from below through bottom post (d3.4 clearance) and the PCB hole (3.2) into the lid post (pilot d2.5).
  Engagement in the lid post 11.1 mm; 2.4 mm of material above the pilot hole end.

## Main dimensions (model frame, Z = 0 at PCB bottom)

| Item | Value |
|---|---|
| Outer size | 192.8 x 44.0 x 22.1 mm (+ feet) |
| Wall / floor / lid top | 1.8 / 2.0 / 2.0 mm; PCB edge to inner wall 0.2 mm |
| Floor top / PCB bottom / PCB top / lid ceiling / lid top | -4.48 / 0 / 1.6 / 13.60 / 15.60 |
| Floor clearance under the longest THT tail (USB-B, -3.08) | 1.4 mm |
| Lid ceiling over the tallest part (USB-B 12.15) | 1.45 mm |
| Button stem tip below the lid top | 1.0 mm (dimple depth 3.5 mm) |
| Terminal screw heads below the lid top | ~4.6 mm (screw top z ≈ 11.0) |
| Connector faces vs outer wall | USB-B / USB-A / input header: flush (0.0); USB-C: 0.6 mm behind -> overmold pocket |
| Opening clearance around connector profiles | 0.3 mm; profile sliced every 0.25 mm through the wall (USB-A flange: 0.5 mm pocket at the face only); input header = Dupont 2x4 envelope (pins + 0.95) + 0.3 |

## Assumptions (provisional, not measured)

- Fit allowances (`Params`): conn_clr 0.3, window_clr 0.25, fit_side 0.15, seam_gap 0.2, pilot d2.5 for a 3.0 mm
  self-tapping screw in PLA, led_hole_d 3.2 - starting values for PLA on the Centauri Carbon, to be confirmed by a
  test print.
- Screw head d6.0 x 2.4 and feet d10 - replace with the bought parts.
- Connector and terminal geometry is taken from the KiCad 3D models in the board STEP (USB-A J402 model is the
  simplified one from D-47, input header J301 is a stand-in, LED model standoff is arbitrary - D-50).
- LED height: LEDs are raised on their leads into the lid holes at assembly (a height jig is not part of this pilot).

## Printing

- Primary print file `out/generic/benchhand_enclosure.3mf` (plain 3MF, no printer settings, D-144); print guide
  `PRINTING.md`. The settings below describe the optional example
  `examples/centauri_carbon_canvas/benchhand_enclosure_centauri_canvas.3mf` (OrcaSlicer project, one 256 mm plate).
- Settings = the OrcaSlicer 2.4.2 system presets the owner uses for CANVAS multi-colour prints (checked against the
  owner's frakce_tarifnik / frakce_kreditnik projects, D-134/D-135): printer "Elegoo Centauri Carbon 0.4 nozzle"
  (automatic change `M6211 T<n>` with purge into the chute, no M600 pause), process "0.20mm Standard @Elegoo CC
  0.4 nozzle" (prime tower on, at x165 y200, only in layers 1-2), 2 x "Generic PLA @System" (220 °C).
  Deliberate overrides: Arachne walls, 0.42 mm first-layer line width, flush black->white 800 / white->black 300 mm³.
- Filament 1 (T0) = black, filament 2 (T1) = white - map them to the CANVAS slots when sending. The start G-code
  loads T0 itself.
- Filament order (D-143, owner after the second print): first-layer sequence 2 -> 1 = white first on the bed, then
  black; layer 2 black then white; black from layer 3: 3 automatic changes (replaces D-133). Pressure advance 0.02 and
  a 30° prime-tower cone taken from the owner's saved project.
- Lid: top face on the bed (textured PEI), labels inlaid flush in layers 1-2 (0.4 mm), top edge chamfer 0.6 mm;
  the white frames sit >= 0.45 mm inside the top-face edge and touch the button dimples (D-143, replaces the 0.15 mm
  chamfer of D-138); no supports needed
  (dimples are 45° cones, notches open upwards in print orientation).
- Bottom: floor on the bed; the screw-head pockets (d6.6, 3.2 mm deep incl. the 0.5 mm d10.5 foot recess) have an
  annular ceiling around the d3.4 hole that does not bridge (first print came out bad) -> the 3MF enables support
  for the bottom object only: normal(auto), on build plate only (D-136). Sliced: support only in the 4 pockets.
- Labels (D-137, round 2 after the first print): Nimbus Sans Narrow Bold (Ubuntu `fonts-urw-base35`), body text
  4.4 mm (cap height 3.4-3.6 mm), product name "BenchHand" 7 mm (D-141/D-143), relay numbers 8 mm, DIGITAL IN
  numbers 5.5 mm, + / - drawn as 1.0 mm strokes 3.6 mm long; outline growth 0.12 mm per side, letter gaps
  >= 0.48 mm, element gaps >= 0.5 mm (black between frames 0.7 mm); lines / frames 0.9 mm, USB path 1.2 mm,
  relay symbol 1.1 mm. Criterion "2 nozzle passes per stroke" is checked on the sliced G-code (the raster value
  `strokes_thinner_0.84` in checks.json counts glyph corners and is only indicative): white extrusion length
  printed as >= 2 lines (width <= 0.55 mm) = 99.3 % in layer 1, 98.9 % in layer 2.
- OrcaSlicer 2.4.2 CLI slice of the example plate: 2 h 06 min, 52.8 g black + 0.9 g white (incl. purge and support),
  3 automatic changes (M6211, 0 x M600): start loads white, layer 1 white -> black, layer 2 black -> white, black from
  layer 3 (layer 3 starts on the prime tower with the change to black). White extrusion as >= 2 lines: 99.4 % (layer 1),
  98.9 % (layer 2). Layer-1/2 toolpaths checked: black between all letters and inside e, a, d, B, O, P, R and the E arms;
  the bottom of the V notch of "M" (COM) and the acute corners of "N" stay unfilled (narrower than one line, also with
  gap-fill filtering off or concentric bottom pattern) - a ~0.4 mm deep dark notch, letters stay separate.
- 3MF placement uses the bounding box of the exported mesh (the OCC box grew ~0.1 mm on the label B-splines, the lid
  floated above the bed and OrcaSlicer refused to slice).

## Validation performed

Last run 2026-11-08 (`out/build_full.log`, `build.py --no-previews --no-regen-test`; previews skipped because the
full preview set ran out of host memory before; the top view was rendered separately).

| Check | Result |
|---|---|
| Parts valid, one solid each | bottom 192.8 x 44.0 x 7.88, lid 192.8 x 44.0 x 15.2, labels 115 solids x 0.4 mm - all valid |
| Exact intersection with all 222 board solids | none |
| Distance < 0.25 mm | only designed contacts: bottom posts on the PCB (0.0), lid posts on the PCB (0.09 = STEP board body is 1.51 mm, real board 1.6) |
| Print meshes | 0 open / 0 non-manifold edges (bottom, lid, labels) |
| STEP re-import | volumes and solid counts match (lid / labels written as NURBS); the assembly STEP reports 1 invalid solid = a component model inside the KiCad board STEP |
| Parameter regeneration (previous run, same code path) | wall 2.2 + dimple 12, and stem_recess 1.5 + gap 0.3: all parts valid, sizes follow |
| Labels | 0 glyph offset failures, min glyph gap 0.48 mm, 0 element gaps < 0.5 mm (incl. dimples, LED holes, slots), >= 0.45 mm black to the lid top edge (D-138) |
| 3MF, OrcaSlicer 2.4.2 CLI | loads (manifold), slices; filament 2 only in layers 1-2; white >= 2 lines on 99 % of its length |

Not verified: physical fit (no print yet), real screw / feet dimensions, the USB-C overmold of the cable actually
used, button feel in the dimple, the parameter-regeneration test and the full preview set for this revision
(only `out/previews/top.png` and `slicer_layer1_top.png` are current).

## Remaining fit questions / suggested test print

1. Print a short test strip first: one dimple + button, one terminal slot + wire window, the USB-B notch and a
   corner post with a screw - checks the 0.3 mm clearances, the pilot hole, the dimple feel and label legibility.
2. USB-C plug overmold vs the 0.6 mm recessed face - check with the cable actually used.
3. PCB outline tolerance (±0.2 mm at JLC) vs the 0.2 mm wall gap - measure the real board.

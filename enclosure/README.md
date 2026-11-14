# BenchHand enclosure (CadQuery)

Two-part 3D-printed enclosure for **BenchHand**, board **USB-DEBUG-CTRL r1, date code 2643**
(`cad/usb_debug_ctrl.kicad_pcb`, assembled STEP `output/manufacturing/USB-DEBUG-CTRL-r1_2643/…_3D.step`).
Decisions: `DECISIONS.md` D-127..D-138, D-141..D-144 (licence: D-148 on main). Design notes and check results: `DESIGN_NOTES.md`.

## Files

| File | Role |
|---|---|
| `enclosure.py` | **Editable master.** `Params` dataclass = every design parameter; `Derived` = dependent dimensions; part builders |
| `graphics.py` | Lid-top label / graphic layout (white inlay), KiCad coordinates |
| `extract_board.py` | Reads the PCB + STEP and writes `board_data.json` (outline, holes, buttons, LEDs, terminal pins, connector profiles at the walls, heights, bottom copper) |
| `board_data.json` | Board-derived dimensions (generated; records SHA-256 of both inputs) |
| `build.py` | One-shot rebuild: geometry → checks → STEP / STL / 3MF → previews |
| `render.py` | Offscreen VTK previews from the real B-rep |
| `label_check.py` | Raster check of label strokes / gaps (run by build.py) |
| `render_hero.py` | README hero renders: enclosure CAD + coloured board GLB |
| `PRINTING.md` | Print and assembly guide |
| `slicer/` | Resolved OrcaSlicer system presets used for the Centauri example 3MF (printer Centauri Carbon 0.4 + CANVAS, process 0.20mm Standard @Elegoo CC, Generic PLA @System) + the owner's project file layout (`centauri_carbon_template.json`) |
| `sketch/` | 2D lid-top design sketches used for the owner discussion (round 2: `lid_top_v2.py`), slicer test plates for text / lines and a G-code layer viewer |
| `out/` | Generated outputs (see below) |

## Environment (tested 2026-11-08, Ubuntu 24.04, Python 3.12.3)

```bash
cd mech/enclosure
python3 -m venv .venv
.venv/bin/pip install --upgrade pip
.venv/bin/pip install -r requirements.txt      # cadquery 2.8.0, cadquery-ocp 7.9.3.1.1, vtk 9.6.2, pillow, scipy
```

Font for the labels: `/usr/share/fonts/opentype/urw-base35/NimbusSansNarrow-Bold.otf` (Ubuntu package `fonts-urw-base35`).

Optional viewer: VS Code extension **OCP CAD Viewer** + `.venv/bin/pip install ocp_vscode`, select `.venv/bin/python`
as the interpreter, then `from ocp_vscode import show; show(*parts.values())`. Generation never needs the viewer.

## Rebuild

```bash
cd mech/enclosure
.venv/bin/python extract_board.py ../../cad/usb_debug_ctrl.kicad_pcb \
    ../../output/manufacturing/USB-DEBUG-CTRL-r1_2643/USB-DEBUG-CTRL-r1_2643_3D.step board_data.json   # only after a board change
.venv/bin/python build.py                     # full: checks + exports + previews
.venv/bin/python build.py --no-previews --no-regen-test   # fast iteration
```

## Coordinate frames

- **KiCad frame** (board_data.json, graphics.py): mm, origin = board top-left corner, X right, **Y down**, top view.
- **Model frame** (all CAD, STEP outputs): X = KiCad X, **Y = −KiCad Y**, Z up, **Z = 0 at the PCB bottom face**,
  PCB top at Z = 1.6. This is the frame of the KiCad STEP export, so the PCB is imported without any transform.
- Print orientation (STL / 3MF only): bottom unchanged (floor on the bed); lid and labels rotated 180° about X and
  lifted so the lid top face is on the bed. The assembly STEP keeps the model frame.

Board revision change: export a new STEP from KiCad, re-run `extract_board.py`, rebuild, re-check. Hard-coded
offsets exist only in `graphics.py` (label positions relative to the board features) and in `extract_board.py`
(probe windows per connector reference).

## Outputs

Print guide: **`PRINTING.md`**. Licence: `LICENSE` (CC BY-SA 4.0, Copyright (c) 2026 Martin Faltus (mafaltu)).

| Output | Content |
|---|---|
| `out/generic/benchhand_enclosure.3mf` | **Primary print file**: plain 3MF (no slicer / printer settings), print orientation; `lid` = `lid_body` (black) + `lid_labels` (white), `bottom` |
| `out/generic/benchhand_enclosure_step.zip` | `bottom.step`, `lid.step`, `lid_labels.step` (model frame, NURBS) |
| `examples/centauri_carbon_canvas/benchhand_enclosure_centauri_canvas.3mf` | Optional example: OrcaSlicer project for Elegoo Centauri Carbon + CANVAS (see its README) |
| `renders/benchhand_closed.png`, `renders/benchhand_exploded.png` | Hero renders with the real coloured board (`render_hero.py`) |
| `out/step/enclosure_assembly.step` (not in git, 70 MB) | Bottom, lid, labels, PCB reference r1_2643 and 4 screws in assembly position |
| `out/stl/*_print.stl` (not in git; release download) | Individual meshes in print orientation (lid and labels share one transform) |
| `out/previews/*.png` (not in git) | Engineering previews; `out/checks.json` machine-readable check results |

## Hero renders

```bash
# board GLB from the released PCB (KiCad 10, headless; never the GUI)
kicad-cli pcb export glb --force --include-silkscreen --include-soldermask --include-pads --include-tracks \
    --include-zones -o board.glb ../../cad/usb_debug_ctrl.kicad_pcb
.venv/bin/python render_hero.py board.glb renders/
```
The LEDs are lifted on their leads to the lid holes (as assembled) and the solder mask / HASL colours are set for the
render only; see the docstring of `render_hero.py`.

# Example: Elegoo Centauri Carbon + CANVAS (OrcaSlicer project)

`benchhand_enclosure_centauri_canvas.3mf` is a ready-to-slice OrcaSlicer / ElegooSlicer project for one specific
setup. It is an example only; the printer-independent files are in `../../out/generic/` (see `../../PRINTING.md`).

| Setting | Value |
|---|---|
| Printer preset | OrcaSlicer 2.4.2 system preset "Elegoo Centauri Carbon 0.4 nozzle" (no printer address stored) |
| Process | "0.20mm Standard @Elegoo CC 0.4 nozzle" + Arachne walls, 0.42 mm first-layer line width |
| Filaments | 2 x "Generic PLA @System": 1 = black (body, bottom), 2 = white (labels) |
| Colour change | CANVAS automatic change (`M6211`), purge into the chute + prime tower (only layers 1-2) |
| Filament order | layer 1 white -> black, layer 2 black -> white, black from layer 3 (3 changes) |
| Supports | bottom part only, from the build plate (screw-head pockets) |

When sending to the printer map filament 1 (T0) to the black CANVAS slot and filament 2 (T1) to the white slot.
If you use your own copy of the printer preset (with the printer address), select it after opening the project.

Sliced with the OrcaSlicer 2.4.2 CLI: 2 h 06 min for both parts, 52.8 g black + 0.9 g white including purge;
pressure advance 0.02 and a 30° prime-tower cone (owner's settings).

Licence: CC BY-SA 4.0, Copyright (c) 2026 Martin Faltus (mafaltu) - see `../../LICENSE`.

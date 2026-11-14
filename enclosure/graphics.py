# SPDX-License-Identifier: CC-BY-SA-4.0
# Copyright (c) 2026 Martin Faltus (mafaltu)
"""Lid-top graphics (white inlay) as 2D primitives in the KiCad frame (mm, Y down).

Layout: owner-approved sketch round 2 (D-137, sketch/out/lid_v2_B_final.png, sketch/lid_top_v2.py variant B with
relay texts 4.4, body 4.4, no HOLD text; title replaced by the product name BenchHand, D-141). Positions come from board_data.json; only the graphic offsets
relative to those features live here.

Stroke rule (D-137): every white stroke is printed with 2 nozzle passes (0.42 mm lines): text Nimbus Sans Narrow
Bold >= 4.4 mm with per-size outline growth, lines >= 0.9 mm. Black between separate elements >= 0.7 mm between
frames, >= 0.5 mm everywhere (checked by enclosure.label_clearance).

Primitive tuples:
  ("text", x, y_baseline, string, font_size, rot_deg[, anchor])  rot 0 or 90 (90 = reads bottom->top);
                                                                  anchor "m" (x = centre, default) / "l" / "r"
  ("seg",  x1, y1, x2, y2, width)                                 round-capped stroke
  ("disc", x, y, r)
  ("poly", [(x, y), ...])
  ("frame", x0, y0, x1, y1, r, width)                             rounded-rectangle outline
"""

LINE = 0.9          # general line / frame width: 2 passes of a 0.42 mm line
USB_W = 1.2         # USB passthrough path (owner: bolder, contact circles must stay distinct)
REL_W = 1.1         # relay switch symbol
FRAME_W = 0.9
FRAME_TOP = -0.45   # frame top edge (KiCad y): 0.45 mm black to the lid top-face edge, inner edge touches the
                    # dimples (owner: touching the dimple is better than reaching the edge, D-139)
FRAME_BOT = 40.5    # frame bottom edge: 0.45 mm black to the lid top-face edge
FRAME_LEFT = -0.5   # USB frame left edge: 0.45 mm black to the lid top-face edge
RELAY_HALF = 8.2    # relay frame half-width: 16.4 at 18.0 pitch -> 0.7 mm black between frames, 0.5 to the slot
BS = 4.4            # body text (USB IN/OUT, NC/NO/COM, DIGITAL IN, 1-4, +TOP/-BOT, PC)
TS = 7.0            # product name "BenchHand" (D-141; owner: smaller, more black around it): 32.7 mm wide
DS = 5.5            # DIGITAL IN numbers 1-4 (owner: a bit larger after the print)
SIGN = 3.6          # + / - signs drawn as strokes (the font's signs printed too small and thin)
SIGN_W = 1.0
NS = 8.0            # relay number beside the LED
ARROW = 4.0         # USB OUT arrow head length


def arrow_down(x, y, s=ARROW):
    return ("poly", [(x, y), (x - s * 0.6, y - s), (x + s * 0.6, y - s)])


def build(bd, outer_x1):
    """bd = board_data dict; outer_x1 = right outer face of the enclosure (KiCad x)."""
    g = []
    terms = sorted(bd["terminals"], key=lambda t: t["pins"][1]["x"])
    leds = {l["ref"]: (l["x"], l["y"]) for l in bd["leds"]}
    com1 = terms[0]["pins"][1]["x"]
    com5 = terms[-1]["pins"][1]["x"]

    # ---- USB passthrough block: USB IN -> open contact -> arrow -> USB OUT (no text, D-137) ----
    gap = FRAME_W + 0.7                          # frame centre lines: 0.7 mm black between neighbouring frames
    g.append(("frame", FRAME_LEFT, FRAME_TOP, com1 - RELAY_HALF - gap, FRAME_BOT, 2.5, FRAME_W))
    cx = (bd["edge_connectors"]["J401"]["a0"] + bd["edge_connectors"]["J401"]["a1"]) / 2
    g.append(("text", cx, 5.6, "USB IN", BS, 0))
    g.append(("seg", cx, 8.0, cx, 15.9, USB_W))
    g.append(("disc", cx, 16.6, 1.0))
    g.append(("seg", cx, 16.6, cx + 3.0, 21.6, USB_W))         # contact drawn open = disconnect action
    g.append(("disc", cx, 22.8, 1.0))
    g.append(("seg", cx, 22.8, cx, 30.0, USB_W))
    g.append(arrow_down(cx, 33.8))
    g.append(("text", cx, 38.8, "USB OUT", BS, 0))

    # ---- relay channels: number beside the LED, NC / NO, SPDT symbol (blade on NC), COM over the slot ----
    for i, t in enumerate(terms):
        nc, com, no = (p["x"] for p in t["pins"])
        lx, _ = leds[f"LED20{i + 1}"]
        g.append(("frame", com - RELAY_HALF, FRAME_TOP, com + RELAY_HALF, FRAME_BOT, 2.5, FRAME_W))
        g.append(("text", lx + 5.6, 17.4, str(i + 1), NS, 0))
        g.append(("text", com - 3.9, 22.0, "NC", BS, 0))
        g.append(("text", com + 3.9, 22.0, "NO", BS, 0))
        g.append(("seg", com - 3.9, 23.6, com - 3.9, 26.0, REL_W))     # NC contact
        g.append(("seg", com + 3.9, 23.6, com + 3.9, 26.0, REL_W))     # NO contact
        g.append(("disc", com, 27.8, 1.0))                               # pivot
        g.append(("seg", com, 27.8, com - 3.5, 26.3, REL_W))            # blade resting on NC
        g.append(("text", com, 32.95, "COM", BS, 0))

    # ---- title, centred over the whole free area right of RELAY 5 (incl. above PC, owner) ----
    t5 = com5 + RELAY_HALF
    tx = (t5 + FRAME_W / 2 + outer_x1 - 0.6) / 2      # centred between RELAY 5 frame and the lid top-face edge
    # one line, vertically centred between the lid top-face edge and the DIGITAL IN frame; the subtitle
    # "USB relay & passthrough controller" does not fit at the 4.4 mm minimum (52-62 mm vs ~46 mm free)
    g.append(("text", tx, 11.8, "BenchHand", TS, 0))

    # ---- PC, rotated, over the USB-C receptacle ----
    j101 = bd["edge_connectors"]["J101"]
    g.append(("text", 188.0, (j101["a0"] + j101["a1"]) / 2, "PC", BS, 90))

    # ---- digital inputs: compact frame, numbers fan out to the + pins, +TOP / -BOT beside them ----
    pins_p = sorted([p for p in bd["inputs_header"]["pins"] if p["net"].endswith("_P")], key=lambda p: p["x"])
    fx0 = t5 + gap                               # 0.7 mm black to the RELAY 5 frame
    pitch = 5.1                                  # number pitch (owner: keep the compact frame width)
    fx1 = fx0 + 4.5 + 3 * pitch + 6.5 + 12.2 + 2.2   # numbers, gap, sign + "BOT" (CAD widths), margin
    g.append(("frame", fx0, 20.4, fx1, FRAME_BOT, 2.5, FRAME_W))
    g.append(("text", (fx0 + fx1) / 2, 25.6, "DIGITAL IN", BS, 0))
    for n, p in enumerate(pins_p):
        nx = fx0 + 4.5 + n * pitch
        g.append(("text", nx, 32.6, str(n + 1), DS, 0))
        g.append(("seg", nx, 33.8, p["x"], 39.0, LINE))
    for yb, word, plus in ((32.4, "TOP", True), (38.2, "BOT", False)):
        g.append(("text", fx1 - 2.2, yb, word, BS, 0, "r"))
        sx, sy, h = fx1 - 2.2 - 8.45 - 1.4 - SIGN / 2, yb - 1.75, SIGN / 2 - SIGN_W / 2    # sign centre, half-arm
        g.append(("seg", sx - h, sy, sx + h, sy, SIGN_W))
        if plus:
            g.append(("seg", sx, sy - h, sx, sy + h, SIGN_W))
    return g

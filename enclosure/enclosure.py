# SPDX-License-Identifier: CC-BY-SA-4.0
# Copyright (c) 2026 Martin Faltus (mafaltu)
"""Parametric two-part enclosure for USB-DEBUG-CTRL (CadQuery).

Model frame: X = KiCad X, Y = -KiCad Y, Z up, Z = 0 at the PCB bottom face (same frame as the
assembled-board STEP exported by KiCad, so the PCB is imported without any transform).

All board-derived positions come from board_data.json (extract_board.py). All design choices are
in the Params dataclass below; dependent dimensions are derived in Derived.
Decisions: DECISIONS.md D-127..D-131.
"""
import json
import math
from dataclasses import dataclass, field, replace
from pathlib import Path

import cadquery as cq
from OCP.BRepOffsetAPI import BRepOffsetAPI_MakeOffset
from OCP.GeomAbs import GeomAbs_JoinType
from OCP.BRepExtrema import BRepExtrema_DistShapeShape

import graphics

HERE = Path(__file__).resolve().parent


@dataclass
class Params:
    # --- shell ---
    wall: float = 1.8               # side wall; gap + wall = 2.0 = connector overhang (D-80) -> faces flush
    gap: float = 0.2                # PCB edge -> inner wall face (JLC outline tolerance +-0.2)
    r_out: float = 3.0              # outer vertical corner radius
    chamfer: float = 0.6            # bed-side outer edge chamfer of the bottom underside
    lid_chamfer: float = 0.6        # lid top edge chamfer (back to 0.6, frames moved inwards instead, D-139)
    label_edge_black: float = 0.45  # min black between any white element and the edge of the lid top face
    floor_t: float = 2.0
    under_clear: float = 1.4        # lowest THT tail -> floor top
    lid_t: float = 2.0              # lid top plate (label inlay is inside this)
    stem_recess: float = 1.0        # button stem tip below the lid top surface (D-129)
    # --- seam (rabbet: lid skirt outside, bottom tongue inside) ---
    skirt_h: float = 1.2
    skirt_t: float = 0.9
    fit_side: float = 0.15          # skirt inner face <-> tongue outer face
    seam_gap: float = 0.2           # vertical gaps at the seam (stack is set by the posts)
    # --- openings ---
    conn_clr: float = 0.3           # around connector profiles in the walls
    min_relief: float = 0.3         # bottom-part relief only if the opening dips >= this below the tongue
    band_clr: float = 0.3           # depth clearance of stepped profile bands (USB-A flange pocket)
    mate_grow: dict = field(default_factory=lambda: {"J301": 0.95})  # Dupont housings: 2.54 pitch envelope
    window_clr: float = 0.25        # around terminal wire openings
    notch_r: float = 0.8            # top-corner radius of wall notches / windows
    dimple_d: float = 11.0          # conical finger dimple, top diameter
    dimple_angle: float = 45.0      # cone half-angle from vertical (45 = printable face-down)
    stem_hole_d: float = 4.0        # button stem (3.5) passage
    dimple_boss_wall: float = 1.2
    led_hole_d: float = 3.2
    tscrew_hole_d: float = 4.0      # screwdriver holes over the terminal screws (when tscrew_slot_w = 0)
    tscrew_slot_w: float = 4.5      # one slot per terminal block over all three screws (owner 2026-11-08)
    usbc_relief: tuple = (12.6, 6.8, 0.6)   # plug overmold pocket (w, h, depth) when the face is recessed
    usbc_relief_min_recess: float = 0.3
    # gussets behind narrow wall fingers between terminal wire windows (owner 2026-11-08)
    finger_max_w: float = 6.0
    finger_rib_t: float = 1.2
    finger_rib_depth_top: float = 7.0   # at the ceiling
    finger_rib_depth_bot: float = 2.0   # at the finger's free end
    finger_rib_z_clear: float = 0.4     # above the PCB top
    # --- fasteners: self-tapping 3 x 16 pan head from below (D-128) ---
    screw_d: float = 3.0
    screw_len: float = 16.0
    head_d: float = 6.0
    head_h: float = 2.4
    head_clear: float = 0.3
    head_pocket_d: float = 6.6
    pilot_d: float = 2.5
    clear_d: float = 3.4
    pilot_extra: float = 0.5        # pilot hole deeper than the screw tip
    top_post_d: float = 7.0         # PCB keep-out is 7 mm
    bot_post_d: float = 9.0
    bot_post_top_d: float = 7.0
    bot_post_top_h: float = 1.0
    post_rib_t: float = 1.6
    # --- feet (cover the screw heads) ---
    foot_d: float = 10.5
    foot_recess: float = 0.5
    # --- PCB floor supports (under buttons and terminal screws) ---
    support_d_max: float = 4.0
    support_d_min: float = 2.4
    support_clear: float = 0.5      # to any bottom-side copper / THT pad
    # --- labels (white inlay, first 2 layers face-down, D-130) ---
    label_t: float = 0.4
    label_bold: float = 0.12        # max glyph outline growth per side
    label_stroke_min: float = 0.88  # target text stroke: 2 nozzle passes of a 0.42 mm line (D-137)
    label_stroke_em: float = 0.128  # thinnest stroke of the font / em (Nimbus Sans Narrow Bold, measured p5)
    font: str = "/usr/share/fonts/opentype/urw-base35/NimbusSansNarrow-Bold.otf"   # Ubuntu fonts-urw-base35
    # --- checks ---
    min_clear_tall: float = 0.5     # tallest component -> lid ceiling


def load_board(path=HERE / "board_data.json"):
    return json.load(open(path))


@dataclass
class Derived:
    p: Params
    bd: dict
    z: dict = field(default_factory=dict)

    def __post_init__(self):
        p, b = self.p, self.bd
        B = b["board"]
        t = B["thickness"]
        # outline in MODEL frame
        self.ix0, self.ix1 = B["x0"] - p.gap, B["x1"] + p.gap
        self.iy0, self.iy1 = -B["y1"] - p.gap, -B["y0"] + p.gap
        self.ox0, self.ox1 = self.ix0 - p.wall, self.ix1 + p.wall
        self.oy0, self.oy1 = self.iy0 - p.wall, self.iy1 + p.wall
        z = self.z
        z["pcb_bot"], z["seam"] = 0.0, t
        z["floor_top"] = b["heights"]["lowest_below_pcb_z"] - p.under_clear
        z["bot"] = z["floor_top"] - p.floor_t
        btn_top = max(s["top_z"] for s in b["buttons"])
        z["top"] = btn_top + p.stem_recess
        z["ceil"] = z["top"] - p.lid_t
        z["tongue_top"] = z["seam"] - p.seam_gap
        z["skirt_bot"] = z["seam"] - p.skirt_h
        z["step"] = z["skirt_bot"] - p.seam_gap
        z["head_seat"] = z["bot"] + p.foot_recess + p.head_h + p.head_clear
        z["screw_tip"] = z["head_seat"] + p.screw_len
        z["pilot_end"] = z["screw_tip"] + p.pilot_extra
        clr = z["ceil"] - b["heights"]["tallest_non_button_z"]
        assert clr >= p.min_clear_tall, f"lid ceiling only {clr:.2f} mm above the tallest part"
        assert z["pilot_end"] < z["top"] - 0.8, "screw too long for the lid post"
        assert z["head_seat"] < z["floor_top"] + (z["pcb_bot"] - z["floor_top"]) - 0.5, "head pocket reaches the PCB"

    def m(self, kx, ky):
        """KiCad (x, y) -> model (x, y)."""
        return kx, -ky


# ------------------------------------------------------------------ helpers
def rbox(x0, x1, y0, y1, z0, z1, r=0.0):
    s = cq.Workplane("XY").workplane(offset=z0).center((x0 + x1) / 2, (y0 + y1) / 2) \
        .rect(x1 - x0, y1 - y0).extrude(z1 - z0)
    if r > 0.05:
        s = s.edges("|Z").fillet(min(r, (x1 - x0) / 2 - 0.01, (y1 - y0) / 2 - 0.01))
    return s


def cyl(x, y, z0, z1, d):
    return cq.Workplane("XY").workplane(offset=z0).center(x, y).circle(d / 2).extrude(z1 - z0)


def notch_box(d, edge, a0, a1, z0, z1, r, inward=1.0, depth=None):
    """Cut prism through a side wall. a0..a1 along the edge in MODEL coords; z0..z1.
    depth = (d0, d1) distance outward from the board edge limits it to part of the wall thickness
    (None = from `inward` inside the inner face to beyond the outer face).
    With rounded top corners the box grows by r*(sqrt2-1) so the clearance survives in the corners."""
    if r > 0:
        g = r * (math.sqrt(2) - 1) + 0.01
        a0, a1, z1 = a0 - g, a1 + g, z1 + g
    B = d.bd["board"]
    d_in, d_out = d.p.gap - inward, d.p.gap + d.p.wall + 1
    if depth is not None:
        d_in, d_out = max(depth[0], d_in), min(depth[1], d_out)
    if edge in ("top", "bottom"):
        e, sg = (-B["y0"], 1) if edge == "top" else (-B["y1"], -1)
        y0, y1 = sorted((e + sg * d_in, e + sg * d_out))
        s = rbox(a0, a1, y0, y1, z0, z1)
        sel = "|Y and >Z"
    else:
        x0, x1 = B["x1"] + d_in, B["x1"] + d_out
        s = rbox(x0, x1, a0, a1, z0, z1)
        sel = "|X and >Z"
    rr = min(r, (a1 - a0) / 2 - 0.05, (z1 - z0) / 2 - 0.05)
    if rr > 0.1:
        s = s.edges(sel).fillet(rr)
    return s


def outer_band(d, edge, a0, a1, z0, z1):
    """Solid filling the skirt band (outer face -> tongue) along an edge range."""
    w = d.p.skirt_t + d.p.fit_side
    if edge == "top":
        return rbox(a0, a1, d.oy1 - w, d.oy1, z0, z1)
    if edge == "bottom":
        return rbox(a0, a1, d.oy0, d.oy0 + w, z0, z1)
    return rbox(d.ox1 - w, d.ox1, a0, a1, z0, z1)


# ------------------------------------------------------------------ openings (shared by both parts)
def wall_openings(d):
    """List of (ref, edge, a0, a1, z0, z1, depth) in MODEL coords, clearances applied.
    Connectors: one entry per profile band (flanges get their own shallow pocket); depth = (d0, d1)
    outward from the board edge, or None for the full wall thickness."""
    p, out = d.p, []
    for ref, c in d.bd["edge_connectors"].items():
        grow = p.mate_grow.get(ref, 0.0)
        bl = c["bands"]
        for i, (d0, d1, a0, a1, z0, z1) in enumerate(bl):
            if c["edge"] == "right":     # along-edge = model Y = -KiCad y
                a0, a1 = -a1, -a0
            dep = (-99 if i == 0 else d0 - p.band_clr, 99 if i == len(bl) - 1 else d1 + p.band_clr)
            out.append((ref, c["edge"], a0 - grow - p.conn_clr, a1 + grow + p.conn_clr,
                        z0 - grow - p.conn_clr, z1 + grow + p.conn_clr, dep))
    for t in d.bd["terminals"]:
        o = t["openings"]
        out.append((t["ref"], "bottom", min(v[0] for v in o) - p.window_clr, max(v[1] for v in o) + p.window_clr,
                    min(v[2] for v in o) - p.window_clr, max(v[3] for v in o) + p.window_clr, None))
    return out


def usbc_recess(d):
    c = d.bd["edge_connectors"]["J101"]
    return d.ox1 - c["face"]


def usbc_relief_cut(d):
    p = d.p
    c = d.bd["edge_connectors"]["J101"]
    if usbc_recess(d) < p.usbc_relief_min_recess:
        return None
    w, h, depth = p.usbc_relief
    yc, zc = -(c["a0"] + c["a1"]) / 2, (c["z0"] + c["z1"]) / 2
    return (cq.Workplane("YZ").workplane(offset=d.ox1 - depth).center(yc, zc)
            .rect(w, h).extrude(depth + 1).edges("|X").fillet(min(h / 2 - 0.01, 3.0)))


# ------------------------------------------------------------------ parts
def build_bottom(d):
    p, z = d.p, d.z
    body = rbox(d.ox0, d.ox1, d.oy0, d.oy1, z["bot"], z["tongue_top"], p.r_out)
    body = body.faces("<Z").edges().chamfer(p.chamfer)
    # cavity
    body = body.cut(rbox(d.ix0, d.ix1, d.iy0, d.iy1, z["floor_top"], z["seam"] + 5, max(p.r_out - p.wall, 0.5)))
    # rabbet: lower the outer band to the step
    w = p.skirt_t + p.fit_side
    ring = rbox(d.ox0 - 1, d.ox1 + 1, d.oy0 - 1, d.oy1 + 1, z["step"], z["seam"] + 5).cut(
        rbox(d.ox0 + w, d.ox1 - w, d.oy0 + w, d.oy1 - w, z["step"] - 1, z["seam"] + 6, max(p.r_out - w, 0.5)))
    body = body.cut(ring)
    # posts, screw passages, head pockets, foot recesses
    for h in d.bd["mount_holes"]:
        x, y = d.m(h["x"], h["y"])
        post = cyl(x, y, z["floor_top"] - 0.1, z["pcb_bot"] - p.bot_post_top_h, p.bot_post_d) \
            .union(cyl(x, y, z["pcb_bot"] - p.bot_post_top_h - 0.01, z["pcb_bot"], p.bot_post_top_d))
        body = body.union(post)
        body = body.cut(cyl(x, y, z["bot"] - 1, z["pcb_bot"] + 1, p.clear_d))
        body = body.cut(cyl(x, y, z["bot"] - 1, z["head_seat"], p.head_pocket_d))
        body = body.cut(cyl(x, y, z["bot"] - 1, z["bot"] + p.foot_recess, p.foot_d))
    # floor supports under buttons / terminal screws
    for (x, y, dd) in support_points(d):
        body = body.union(cyl(x, y, z["floor_top"] - 0.1, z["pcb_bot"], dd))
    # wall openings that dip below the seam -> relief; elsewhere fill the skirt band under notches
    ops = wall_openings(d)
    for ref, edge, a0, a1, z0, z1, dep in ops:
        # relief only where an opening dips clearly below the tongue (a 0.1 mm sliver is not worth it)
        if z0 < z["tongue_top"] - p.min_relief:
            body = body.cut(notch_box(d, edge, a0, a1, z0, z["seam"] + 5, 0, depth=dep))
    skirt_in = p.gap + p.wall - (p.skirt_t + p.fit_side)          # skirt band, distance from the board edge
    for ref in dict.fromkeys(o[0] for o in ops):
        band = [o for o in ops if o[0] == ref and (o[6] is None or o[6][1] > skirt_in)]
        if not band:
            continue
        edge = band[0][1]
        a0, a1 = min(o[2] for o in band), max(o[3] for o in band)
        z_low = min(o[4] for o in band)                           # lowest material of ANY band in the skirt
        top_fill = z_low if z_low < z["tongue_top"] - p.min_relief else z["tongue_top"]
        if top_fill > z["step"] + 0.05:      # the lid skirt is notched here -> raise the bottom's outer band
            body = body.union(outer_band(d, edge, a0, a1, z["step"], top_fill))
    rc = usbc_relief_cut(d)
    if rc is not None:
        body = body.cut(rc)
    return body


def build_lid(d):
    p, z = d.p, d.z
    body = rbox(d.ox0, d.ox1, d.oy0, d.oy1, z["skirt_bot"], z["top"], p.r_out)
    body = body.faces(">Z").edges().chamfer(p.lid_chamfer)
    # cavity above the seam and inside the skirt below it
    body = body.cut(rbox(d.ix0, d.ix1, d.iy0, d.iy1, z["seam"] - 5, z["ceil"], max(p.r_out - p.wall, 0.5)))
    w = p.skirt_t
    body = body.cut(rbox(d.ox0 + w, d.ox1 - w, d.oy0 + w, d.oy1 - w, z["skirt_bot"] - 1, z["seam"],
                         max(p.r_out - w, 0.5)))
    # posts with pilot holes + ribs to the two nearest walls
    for h in d.bd["mount_holes"]:
        x, y = d.m(h["x"], h["y"])
        post = cyl(x, y, z["seam"], z["ceil"] + 0.1, p.top_post_d)
        wx = d.ix0 if abs(x - d.ix0) < abs(x - d.ix1) else d.ix1
        wy = d.iy0 if abs(y - d.iy0) < abs(y - d.iy1) else d.iy1
        post = post.union(rbox(min(x, wx), max(x, wx), y - p.post_rib_t / 2, y + p.post_rib_t / 2,
                               z["seam"], z["ceil"] + 0.1))
        post = post.union(rbox(x - p.post_rib_t / 2, x + p.post_rib_t / 2, min(y, wy), max(y, wy),
                               z["seam"], z["ceil"] + 0.1))
        body = body.union(post)
        body = body.cut(cyl(x, y, z["seam"] - 1, z["pilot_end"], p.pilot_d))
    # button dimples
    for s in d.bd["buttons"]:
        x, y = d.m(s["x"], s["y"])
        R, rh = p.dimple_d / 2, p.stem_hole_d / 2
        depth = (R - rh) / math.tan(math.radians(p.dimple_angle))
        zc = z["top"] - depth
        r_ceil = rh + (z["ceil"] - zc) * math.tan(math.radians(p.dimple_angle))
        boss_bot = zc - p.dimple_boss_wall
        if boss_bot < z["ceil"]:
            body = body.union(cyl(x, y, boss_bot, z["ceil"] + 0.1, 2 * (max(r_ceil, rh) + p.dimple_boss_wall)))
        body = body.cut(cq.Workplane().add(cq.Solid.makeCone(rh, R + 0.02 * depth, depth * 1.02,
                                                             cq.Vector(x, y, zc), cq.Vector(0, 0, 1))))
        body = body.cut(cyl(x, y, boss_bot - 1, z["top"] + 1, p.stem_hole_d))
    for l in d.bd["leds"]:
        x, y = d.m(l["x"], l["y"])
        body = body.cut(cyl(x, y, z["ceil"] - 1, z["top"] + 1, p.led_hole_d))
    for t in d.bd["terminals"]:
        if p.tscrew_slot_w > 0:     # one rounded slot over the three screws (any normal screwdriver)
            (x0, y0), (x1, y1) = d.m(t["pins"][0]["x"], t["pins"][0]["y"]), d.m(t["pins"][2]["x"], t["pins"][2]["y"])
            L = math.hypot(x1 - x0, y1 - y0) + p.tscrew_slot_w
            body = body.cut(cq.Workplane("XY").workplane(offset=z["ceil"] - 1).center((x0 + x1) / 2, (y0 + y1) / 2)
                            .slot2D(L, p.tscrew_slot_w, 0).extrude(z["top"] - z["ceil"] + 2))
        else:
            for pin in t["pins"]:
                x, y = d.m(pin["x"], pin["y"])
                body = body.cut(cyl(x, y, z["ceil"] - 1, z["top"] + 1, p.tscrew_hole_d))
    # wall notches (open towards the seam so the lid drops over the connectors)
    for ref, edge, a0, a1, z0, z1, dep in wall_openings(d):
        body = body.cut(notch_box(d, edge, a0, a1, z["skirt_bot"] - 1, z1, p.notch_r, depth=dep))
    # gussets behind narrow wall fingers left between neighbouring notches (free end at the seam)
    for edge, xc, w in wall_fingers(d):
        body = body.union(finger_rib(d, edge, xc))
    rc = usbc_relief_cut(d)
    if rc is not None:
        body = body.cut(rc)
    return body


def wall_fingers(d):
    """(edge, centre along the edge, width) of wall pieces narrower than finger_max_w between two notches."""
    p, out = d.p, []
    r_g = p.notch_r * (math.sqrt(2) - 1) + 0.01 if p.notch_r > 0 else 0
    for edge in ("top", "bottom", "right"):
        ops = sorted((a0 - r_g, a1 + r_g) for _, e, a0, a1, _, _, _ in wall_openings(d) if e == edge)
        for (_, e0), (s1, _) in zip(ops, ops[1:]):
            w = s1 - e0
            if 0 < w < p.finger_max_w:
                out.append((edge, (e0 + s1) / 2, round(w, 2)))
    return out


def finger_rib(d, edge, c):
    """Trapezoid rib on the inner wall face: deep at the ceiling, shallow at the finger's free end."""
    p, z = d.p, d.z
    zb = z["seam"] + p.finger_rib_z_clear
    wall_y = {"bottom": d.iy0, "top": d.iy1}[edge]
    s = -1 if edge == "top" else 1          # inward direction (model Y)
    pts = [(wall_y - s * 0.3, zb), (wall_y + s * p.finger_rib_depth_bot, zb),
           (wall_y + s * p.finger_rib_depth_top, z["ceil"] + 0.1), (wall_y - s * 0.3, z["ceil"] + 0.1)]
    return cq.Workplane("YZ").workplane(offset=c - p.finger_rib_t / 2).polyline(pts).close() \
        .extrude(p.finger_rib_t)


def support_points(d):
    """Floor pillars under the buttons and the terminal-screw row, placed clear of bottom copper."""
    p, bd = d.p, d.bd
    cu = bd["bottom_copper"]
    targets = [(s["x"], s["y"]) for s in bd["buttons"]]
    targets += [(t["pins"][1]["x"], t["pins"][1]["y"] - 3.0) for t in bd["terminals"]]
    out = []
    for tx, ty in targets:
        best = None
        for i in range(-12, 13):
            for j in range(-12, 13):
                x, y = tx + i * 0.25, ty + j * 0.25
                free = min(math.hypot(x - cx, y - cy) - r for cx, cy, r in cu) - p.support_clear
                edge = min(x - bd["board"]["x0"], bd["board"]["x1"] - x, y - bd["board"]["y0"], bd["board"]["y1"] - y)
                free = min(free, edge - 0.5)
                dd = min(2 * free, p.support_d_max)
                score = dd - 0.15 * math.hypot(x - tx, y - ty)
                if dd >= p.support_d_min and (best is None or score > best[0]):
                    best = (score, x, y, dd)
        if best:
            out.append((best[1], -best[2], round(best[3], 2)))
    return out


# ------------------------------------------------------------------ labels
BOLD_FAILURES = []


def bold_for(size, p):
    """Per-side glyph growth so the thinnest stroke (label_stroke_em x size) reaches label_stroke_min;
    large text is not thickened at all, which keeps the counters (E arms, R bowl) as open as possible."""
    return round(min(p.label_bold, max(0.0, (p.label_stroke_min - p.label_stroke_em * size) / 2)), 3)
TEXT_GAPS = []        # (text, approx pair, gap) for every pair of neighbouring glyph clusters
LETTER_EXTRA = 0.20    # extra tracking on top of 2 x bold (Nimbus Narrow, sizes >= 4.4: slicer test D-137)


def _text(s, size, font, bold=0.0):
    w = cq.Workplane("XY").text(s, size, 1.0, fontPath=font, halign="center", valign="bottom", combine=True)
    if bold <= 0 and LETTER_EXTRA <= 0:
        return w
    # thicken every glyph outline by `bold` per side (2D offset of the glyph faces, holes handled by OCC)
    # and add the same amount of letter spacing so the gaps between glyphs keep their original width
    faces = sorted(w.faces("<Z").vals(), key=lambda f: f.BoundingBox().xmin)
    rank, cl_xmax, k = [], None, -1
    for f in faces:                      # glyph clusters = faces overlapping in x (i-dot, accents)
        bb = f.BoundingBox()
        if cl_xmax is None or bb.xmin > cl_xmax - 0.05:
            k += 1
            cl_xmax = bb.xmax
        else:
            cl_xmax = max(cl_xmax, bb.xmax)
        rank.append(k)
    track = 2 * bold + LETTER_EXTRA
    solids = []
    for f, rk in zip(faces, rank):
        face = f if bold <= 0 else None
        for d in (bold, bold * 0.75, bold * 0.5) if face is None else ():   # OCC offset can fail on B-spline glyphs
            try:
                mk = BRepOffsetAPI_MakeOffset(f.wrapped, GeomAbs_JoinType.GeomAbs_Arc)
                mk.Perform(d)
                wires = cq.Shape.cast(mk.Shape()).Wires()
                wires.sort(key=lambda x: x.BoundingBox().DiagonalLength, reverse=True)
                cand = cq.Face.makeFromWires(wires[0], wires[1:]).fix()
                if cand.isValid() and cand.Area() > f.Area():
                    face = cand
                    break
            except Exception:
                continue
        if face is None and bold > 0:                         # fallback: offset each wire separately
            try:
                outer = f.outerWire().offset2D(bold, "arc")[0]
                inners = []
                for iw in f.innerWires():
                    c = [ww for dd in (bold, -bold) for ww in iw.offset2D(dd, "arc")]
                    inners.append(min(c, key=lambda x: x.BoundingBox().DiagonalLength))
                cand = cq.Face.makeFromWires(outer, inners).fix()
                if cand.isValid() and cand.Area() > f.Area():
                    face = cand
            except Exception:
                pass
        if face is None:
            BOLD_FAILURES.append(s)
            face = f
        face = face.translate(cq.Vector((rk - k / 2) * track, 0, 0))
        solids.append(cq.Solid.extrudeLinear(face, cq.Vector(0, 0, 1.0)))
    clusters = [cq.Compound.makeCompound([so for so, r in zip(solids, rank) if r == i]) for i in range(k + 1)]
    for i in range(k):
        dist = BRepExtrema_DistShapeShape(clusters[i].wrapped, clusters[i + 1].wrapped)
        TEXT_GAPS.append((s, s[i:i + 2] if len(s) > i + 1 else s, round(dist.Value(), 3)))
    return cq.Workplane().add(solids[0].fuse(*solids[1:]).clean() if len(solids) > 1 else solids[0])


def build_label_tool(d):
    """All graphics as one solid, z from top - label_t to top (MODEL frame)."""
    p, z = d.p, d.z
    t = p.label_t
    parts, names = [], []
    for g in graphics.build(d.bd, d.ox1):
        kind = g[0]
        names.append(f"{kind}:{g[3]}" if kind == "text" else f"{kind}@{tuple(round(v, 1) for v in g[1:3])}"
                     if kind != "poly" else "arrow")
        if kind == "text":
            _, x, y, s, size, rot = g[:6]
            anchor = g[6] if len(g) > 6 else "m"
            w = _text(s, size, p.font, bold_for(size, p))
            if anchor != "m":                     # left / right aligned at x (rot 0 only)
                bb = w.val().BoundingBox()
                w = w.translate((-bb.xmin if anchor == "l" else -bb.xmax, 0, 0))
            if rot:
                w = w.rotate((0, 0, 0), (0, 0, 1), rot)
            mx, my = d.m(x, y)
            parts.append(w.translate((mx, my, 0)).val())
        elif kind == "seg":
            _, x1, y1, x2, y2, wd = g
            (a, b), (c, e) = d.m(x1, y1), d.m(x2, y2)
            L = math.hypot(c - a, e - b)
            ang = math.degrees(math.atan2(e - b, c - a))
            s = cq.Workplane("XY").center((a + c) / 2, (b + e) / 2).slot2D(L + wd, wd, ang).extrude(1.0)
            parts.append(s.val())
        elif kind == "disc":
            _, x, y, r = g
            parts.append(cq.Workplane("XY").center(*d.m(x, y)).circle(r).extrude(1.0).val())
        elif kind == "poly":
            pts = [d.m(x, y) for x, y in g[1]]
            parts.append(cq.Workplane("XY").polyline(pts).close().extrude(1.0).val())
        elif kind == "frame":
            _, x0, y0, x1, y1, r, wd = g
            (a, b), (c, e) = d.m(x0, y0), d.m(x1, y1)
            xa, xb, ya, yb = min(a, c), max(a, c), min(b, e), max(b, e)
            outer = cq.Workplane("XY").center((xa + xb) / 2, (ya + yb) / 2).rect(xb - xa + wd, yb - ya + wd) \
                .extrude(1.0).edges("|Z").fillet(r + wd / 2)
            inner = cq.Workplane("XY").center((xa + xb) / 2, (ya + yb) / 2).rect(xb - xa - wd, yb - ya - wd) \
                .extrude(1.0).edges("|Z").fillet(r - wd / 2)
            parts.append(outer.cut(inner).val())
    d.label_parts = list(zip(names, parts))
    # extruded 1.0 mm from top - label_t; trimmed to label_t by the lid top slab in build_labels()
    fused = parts[0].fuse(*parts[1:]).clean()
    return cq.Workplane().add(fused).translate((0, 0, z["top"] - t))


def build_labels(d, lid):
    """Label inlay = graphics ∩ lid top slab (so holes / chamfer stay empty)."""
    p, z = d.p, d.z
    tool = build_label_tool(d)
    top_slab = lid.intersect(rbox(d.ox0 - 1, d.ox1 + 1, d.oy0 - 1, d.oy1 + 1, z["top"] - p.label_t, z["top"] + 1))
    labels = top_slab.intersect(tool)
    return labels


def build_all(p=None, bd=None):
    p = p or Params()
    bd = bd or load_board()
    d = Derived(p, bd)
    bottom = build_bottom(d)
    lid_raw = build_lid(d)
    labels = build_labels(d, lid_raw)
    lid = lid_raw.cut(labels)
    return d, {"bottom": bottom, "lid": lid, "labels": labels}


def label_clearance(d, min_gap=0.5):
    """2D gaps between separate white elements and between white elements and lid openings (model frame)."""
    p, z = d.p, d.z
    items = list(d.label_parts)
    holes = []
    for b in d.bd["buttons"]:
        holes.append((f"dimple {b['ref']}", cyl(*d.m(b["x"], b["y"]), 0, 1, p.dimple_d).val()))
    for l in d.bd["leds"]:
        holes.append((f"led hole {l['ref']}", cyl(*d.m(l["x"], l["y"]), 0, 1, p.led_hole_d).val()))
    for t in d.bd["terminals"]:
        (x0, y0), (x1, y1) = d.m(t["pins"][0]["x"], t["pins"][0]["y"]), d.m(t["pins"][2]["x"], t["pins"][2]["y"])
        holes.append((f"slot {t['ref']}", cq.Workplane("XY").center((x0 + x1) / 2, y0)
                      .slot2D(x1 - x0 + p.tscrew_slot_w, p.tscrew_slot_w, 0).extrude(1).val()))
    out = []
    # edge of the lid top face (inside the chamfer): every white element keeps label_edge_black of black to it
    ch = p.lid_chamfer
    face_edge = rbox(d.ox0 + ch, d.ox1 - ch, d.oy0 + ch, d.oy1 - ch, 0, 1, p.r_out - ch).faces("#Z").vals()
    edge = cq.Compound.makeCompound(face_edge)
    for na, a in items:
        dist = BRepExtrema_DistShapeShape(a.wrapped, edge.wrapped).Value()
        if dist < p.label_edge_black - 1e-3:
            out.append({"a": na, "b": "lid top edge", "gap_mm": round(dist, 3)})
    every = items + holes
    for i, (na, a) in enumerate(items):
        ba = a.BoundingBox()
        for nb, b in every[i + 1:]:
            bb = b.BoundingBox()
            if ba.xmax + min_gap < bb.xmin or bb.xmax + min_gap < ba.xmin or ba.ymax + min_gap < bb.ymin \
                    or bb.ymax + min_gap < ba.ymin:
                continue
            dist = BRepExtrema_DistShapeShape(a.wrapped, b.wrapped).Value()
            designed = dist < 1e-3 and na.split("@")[0] in ("seg", "disc", "arrow") \
                and nb.split("@")[0] in ("seg", "disc", "arrow")      # joined strokes of one symbol
            # frame top edges touch the dimples by design (owner, D-139): the dimple cuts the white there
            designed = designed or (na.startswith("frame") and nb.startswith("dimple"))
            if dist < min_gap and not designed:
                out.append({"a": na, "b": nb, "gap_mm": round(dist, 3)})
    # touching graphic parts that are meant to touch (switch symbol, relay contact) are listed too;
    # the review decides - see DESIGN_NOTES.md
    return out

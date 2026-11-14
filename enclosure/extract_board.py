# SPDX-License-Identifier: CC-BY-SA-4.0
# Copyright (c) 2026 Martin Faltus (mafaltu)
"""Extract the mechanical data the enclosure needs from the KiCad board and its assembled STEP.

Writes board_data.json (the single source of board-derived dimensions for enclosure.py).
Re-run after every board revision; the JSON records the SHA-256 of both inputs.

Frames
------
KiCad frame (stored in the JSON): mm, origin = page origin used by the board (board top-left
corner is at 0,0 for this board), X right, Y DOWN, top view.
Model frame (STEP and enclosure): X = KiCad X, Y = -KiCad Y, Z up, Z = 0 at the PCB BOTTOM face.
STEP-derived values below are converted back to the KiCad frame (y_kicad = -y_model); z values
are model Z.

Usage: python extract_board.py <board.kicad_pcb> <board_3d.step> <out.json>
"""
import hashlib
import json
import math
import re
import sys

import cadquery as cq


def rel(path):
    import os
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    return os.path.relpath(os.path.abspath(path), root)


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


# ---------------------------------------------------------------- KiCad parsing
def parse_pcb(text):
    fps = {}
    for m in re.finditer(r'\n\t\(footprint "([^"]+)"(.*?)\n\t\)', text, re.S):
        lib, body = m.group(1), m.group(2)
        ref = re.search(r'\(property "Reference" "([^"]+)"', body).group(1)
        x, y, r = re.search(r'\(at ([\d.\-]+) ([\d.\-]+)(?: ([\d.\-]+))?\)', body).groups()
        x, y, a = float(x), float(y), math.radians(float(r) if r else 0.0)
        layer = re.search(r'\(layer "([^"]+)"\)', body).group(1)
        pads = []
        for p in re.finditer(r'\(pad "([^"]*)" (\w+) (\w+)\s+\(at ([\d.\-]+) ([\d.\-]+)[^)]*\)(.*?)\n\t\t\)',
                             body, re.S):
            px, py = float(p.group(4)), float(p.group(5))
            ax = x + px * math.cos(a) + py * math.sin(a)
            ay = y - px * math.sin(a) + py * math.cos(a)
            pb = p.group(6)
            size = re.search(r'\(size ([\d.]+) ([\d.]+)\)', pb)
            net = re.search(r'\(net (?:\d+ )?"([^"]*)"', pb)
            layers = re.search(r'\(layers ([^)]*)\)', pb)
            pads.append({
                "name": p.group(1), "type": p.group(2), "x": round(ax, 4), "y": round(ay, 4),
                "size": [float(size.group(1)), float(size.group(2))] if size else [0, 0],
                "net": net.group(1) if net else "",
                "layers": layers.group(1) if layers else "",
            })
        fps[ref] = {"lib": lib, "x": x, "y": y, "rot": math.degrees(a), "layer": layer, "pads": pads}
    vias = [{"x": float(v.group(1)), "y": float(v.group(2)), "d": float(v.group(3))}
            for v in re.finditer(r'\(via\s+\(at ([\d.\-]+) ([\d.\-]+)\)\s+\(size ([\d.]+)\)', text)]
    edges = []
    for m in re.finditer(r'\(gr_(line|arc)\s+\(start ([\d.\-]+) ([\d.\-]+)\)\s+(?:\(mid ([\d.\-]+) ([\d.\-]+)\)\s+)?'
                         r'\(end ([\d.\-]+) ([\d.\-]+)\).*?\(layer "Edge\.Cuts"\)', text, re.S):
        g = m.groups()
        edges.append({"kind": g[0], "start": [float(g[1]), float(g[2])], "end": [float(g[5]), float(g[6])]})
    thick = float(re.search(r'\(general\s+\(thickness ([\d.]+)\)', text).group(1))
    return fps, vias, edges, thick


def centroid(fp):
    xs = [p["x"] for p in fp["pads"] if p["name"]]
    ys = [p["y"] for p in fp["pads"] if p["name"]]
    return sum(xs) / len(xs), sum(ys) / len(ys)


# ---------------------------------------------------------------- STEP analysis
class Step:
    def __init__(self, path):
        self.solids = cq.importers.importStep(path).val().Solids()
        self.bbs = [s.BoundingBox() for s in self.solids]

    def in_box(self, x0, x1, y0, y1, z0, z1):
        """Union bbox of all solid material inside the model-frame box (exact boolean)."""
        box = cq.Solid.makeBox(x1 - x0, y1 - y0, z1 - z0, cq.Vector(x0, y0, z0))
        acc = None
        for s, b in zip(self.solids, self.bbs):
            if b.xmax < x0 or b.xmin > x1 or b.ymax < y0 or b.ymin > y1 or b.zmax < z0 or b.zmin > z1:
                continue
            if b.xlen > 100:       # the bare board itself, not a component
                continue
            i = s.intersect(box)
            if i.Volume() > 1e-6:
                bb = i.BoundingBox()
                v = [bb.xmin, bb.xmax, bb.ymin, bb.ymax, bb.zmin, bb.zmax]
                acc = v if acc is None else [min(acc[0], v[0]), max(acc[1], v[1]), min(acc[2], v[2]),
                                             max(acc[3], v[3]), min(acc[4], v[4]), max(acc[5], v[5])]
        return acc

    def max_z_near(self, x, y, r):
        z = None
        for b in self.bbs:
            cx, cy = (b.xmin + b.xmax) / 2, (b.ymin + b.ymax) / 2
            if abs(cx - x) < r and abs(cy - y) < r:
                z = b.zmax if z is None else max(z, b.zmax)
        return z

    def openings(self, x0, x1, y_face, z0, z1):
        """Inner wires (holes) of the cross-section of solids at plane y = y_face (model frame)."""
        out = []
        slab = cq.Solid.makeBox(x1 - x0, 0.05, z1 - z0, cq.Vector(x0, y_face, z0))
        for s, b in zip(self.solids, self.bbs):
            if b.xmax < x0 or b.xmin > x1 or b.ymax < y_face or b.ymin > y_face + 0.05:
                continue
            sec = s.intersect(slab)
            for f in sec.Faces():
                if abs(f.normalAt().y) > 0.9:
                    for w in f.innerWires():
                        bb = w.BoundingBox()
                        out.append([round(bb.xmin, 3), round(bb.xmax, 3), round(bb.zmin, 3), round(bb.zmax, 3)])
                    break
        return sorted({tuple(o) for o in out})


def bands(step, edge, a0, a1, bx1, by0, by1, d_min, d_max, dstep=0.25):
    """Connector profile sliced every dstep across the wall zone; consecutive equal slices merged.
    Returns [[d0, d1, a0, a1, z0, z1], ...]: d = distance OUTWARD from the board edge, a along the edge
    (KiCad coords), z model Z. Captures flanges / steps instead of one bounding box."""
    out = []
    n = int(round((d_max - d_min) / dstep))
    for i in range(n):
        da, db = d_min + i * dstep, d_min + (i + 1) * dstep
        if edge == "top":
            bb = step.in_box(a0, a1, -by0 + da, -by0 + db, -10, 40)
            prof = None if bb is None else (bb[0], bb[1], bb[4], bb[5])
        elif edge == "bottom":
            bb = step.in_box(a0, a1, -by1 - db, -by1 - da, -10, 40)
            prof = None if bb is None else (bb[0], bb[1], bb[4], bb[5])
        else:
            bb = step.in_box(bx1 + da, bx1 + db, -a1, -a0, -10, 40)
            prof = None if bb is None else (-bb[3], -bb[2], bb[4], bb[5])
        if prof is None:
            continue
        prof = [round(v, 3) for v in prof]
        if out and abs(out[-1][1] - da) < 1e-6 and all(abs(u - v) < 0.02 for u, v in zip(out[-1][2:], prof)):
            out[-1][1] = round(db, 3)
        else:
            out.append([round(da, 3), round(db, 3)] + prof)
    return out


def main(pcb_path, step_path, out_path):
    fps, vias, edges, thick = parse_pcb(open(pcb_path).read())
    xs = [c for e in edges for c in (e["start"][0], e["end"][0])]
    ys = [c for e in edges for c in (e["start"][1], e["end"][1])]
    bx0, bx1, by0, by1 = min(xs), max(xs), min(ys), max(ys)
    # outline segments not on the bounding rectangle (internal cut-outs / stray lines) -> reported
    stray = [e for e in edges if e["kind"] == "line" and not (
        (e["start"][0] == e["end"][0] and e["start"][0] in (bx0, bx1)) or
        (e["start"][1] == e["end"][1] and e["start"][1] in (by0, by1)))]

    step = Step(step_path)
    data = {
        # paths stored relative to the repository root (two levels above this file)
        "source": {"pcb": rel(pcb_path), "pcb_sha256": sha(pcb_path), "step": rel(step_path),
                   "step_sha256": sha(step_path)},
        "frame": "KiCad x/y (y down); z = model Z, 0 at PCB bottom",
        "board": {"x0": bx0, "x1": bx1, "y0": by0, "y1": by1, "thickness": thick, "corner_r": 1.0,
                  "stray_edge_segments": stray},
        "mount_holes": [], "buttons": [], "leds": [], "terminals": [], "inputs_header": {},
        "edge_connectors": {}, "bottom_copper": [],
    }
    for ref in sorted(fps):
        fp = fps[ref]
        if ref.startswith("MH"):
            d = fp["pads"][0]["size"][0] if fp["pads"] else 3.2
            data["mount_holes"].append({"ref": ref, "x": fp["x"], "y": fp["y"], "drill": d})
        elif ref.startswith("SW"):
            cx, cy = centroid(fp)
            data["buttons"].append({"ref": ref, "x": cx, "y": cy, "top_z": step.max_z_near(cx, -cy, 2.5)})
        elif ref.startswith("LED"):
            cx, cy = centroid(fp)
            data["leds"].append({"ref": ref, "x": round(cx, 3), "y": cy})
        elif re.fullmatch(r"J20\d", ref):
            pins = {p["name"]: p for p in fp["pads"]}
            role = {"1": "NC", "2": "COM", "3": "NO"}
            for n, p in pins.items():
                assert p["net"].endswith(role[n]), (ref, n, p["net"])     # NC / COM / NO order check
            data["terminals"].append({"ref": ref, "pins": [{"role": role[n], "x": pins[n]["x"], "y": pins[n]["y"]}
                                                           for n in ("1", "2", "3")]})
    # bottom-side copper (THT pads, bottom SMD pads, vias) for floor support placement
    for ref, fp in fps.items():
        for p in fp["pads"]:
            if p["type"] in ("thru_hole", "np_thru_hole") or "B.Cu" in p["layers"]:
                data["bottom_copper"].append([round(p["x"], 3), round(p["y"], 3), round(max(p["size"]) / 2, 3)])
    for v in vias:
        data["bottom_copper"].append([v["x"], v["y"], v["d"] / 2])

    j301 = fps["J301"]
    data["inputs_header"] = {"ref": "J301", "pins": [{"name": p["name"], "x": p["x"], "y": p["y"], "net": p["net"]}
                                                     for p in j301["pads"]]}

    # ---- STEP: profiles of everything protruding beyond the board edge, per connector ----
    D = 4.0   # probe depth beyond the edge
    IN_ = 0.8  # probe depth inside the edge (wall gap 0.2 + 0.6 margin)
    regions = {   # ref: (edge, along-edge window in KiCad coords)
        "J401": ("top", fps["J401"]["x"] - 12, fps["J401"]["x"] + 12),
        "J402": ("bottom", 0.0, 40.0),
        "J301": ("bottom", 140.0, 165.0),
        "J101": ("right", 5.0, 35.0),
    }
    for ref, (edge, a0, a1) in regions.items():
        # probe starts IN_ inside the board edge so bodies hugging the inner wall face are included
        if edge == "top":
            bb = step.in_box(a0, a1, -by0 - IN_, -by0 + D, -10, 40)
        elif edge == "bottom":
            bb = step.in_box(a0, a1, -by1 - D, -by1 + IN_, -10, 40)
        else:
            bb = step.in_box(bx1 - IN_, bx1 + D, -a1, -a0, -10, 40)
        if bb is None:
            raise SystemExit(f"no geometry beyond the {edge} edge for {ref}")
        x0, x1, y0, y1, z0, z1 = bb
        data["edge_connectors"][ref] = {
            "edge": edge,
            # along-edge extent in KiCad coords, face = outermost point, z range (model Z)
            "a0": round(x0 if edge != "right" else -y1, 3), "a1": round(x1 if edge != "right" else -y0, 3),
            "face": round({"top": -y1, "bottom": -y0, "right": x1}[edge], 3),
            "z0": round(z0, 3), "z1": round(z1, 3),
            "bands": bands(step, edge, a0, a1, bx1, by0, by1, -IN_, D),
        }
    # ---- terminal front wire openings (section 0.2 mm behind the front face) ----
    for t in data["terminals"]:
        xs = [p["x"] for p in t["pins"]]
        x0, x1 = min(xs) - 6, max(xs) + 6
        # front face: lowest model-y of terminal material near the pins
        bb = step.in_box(x0, x1, -by1, -t["pins"][0]["y"], thick + 0.01, 20)
        t["front_y"] = round(-bb[2], 3)
        t["body"] = [round(bb[0], 3), round(bb[1], 3), round(bb[4], 3), round(bb[5], 3)]
        t["openings"] = step.openings(x0, x1, bb[2] + 0.2, thick, 20)
    data["heights"] = {
        "lowest_below_pcb_z": round(min(b.zmin for b in step.bbs), 3),
        "tallest_non_button_z": round(max(b.zmax for b in step.bbs
                                          if not any(abs((b.xmin + b.xmax) / 2 - s["x"]) < 3 and
                                                     abs((b.ymin + b.ymax) / 2 + s["y"]) < 3
                                                     for s in data["buttons"])), 3),
    }
    json.dump(data, open(out_path, "w"), indent=1)
    print(json.dumps({k: data[k] for k in ("board", "heights", "edge_connectors")}, indent=1))
    print("terminals", [(t["ref"], t["front_y"], t["openings"]) for t in data["terminals"]])
    print("buttons", [(b["ref"], b["top_z"]) for b in data["buttons"]])


if __name__ == "__main__":
    main(*sys.argv[1:4])

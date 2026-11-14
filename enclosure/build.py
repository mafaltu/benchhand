# SPDX-License-Identifier: CC-BY-SA-4.0
# Copyright (c) 2026 Martin Faltus (mafaltu)
"""Rebuild the enclosure, run the checks, export STEP / STL / 3MF and render previews.

Usage (from mech/enclosure):  .venv/bin/python build.py [--no-previews] [--no-regen-test]
Outputs go to mech/enclosure/out/ (see README.md).
"""
import argparse
import json
import math
import sys
import time
import uuid
import zipfile
from pathlib import Path

import cadquery as cq
from OCP.BRepExtrema import BRepExtrema_DistShapeShape

import enclosure as E
from extract_board import parse_pcb

HERE = Path(__file__).resolve().parent
OUT = HERE / "out"
COL = {"bottom": (0.13, 0.13, 0.14), "lid": (0.16, 0.16, 0.17), "labels": (0.96, 0.96, 0.94),
       "pcb": (0.10, 0.42, 0.22), "comp": (0.72, 0.72, 0.74), "screw": (0.75, 0.6, 0.25)}
PLATE = 256.0     # Elegoo Centauri Carbon build plate (mm)


def log(*a):
    print(*a, flush=True)


def solid(w):
    vals = w.vals()
    return vals[0] if len(vals) == 1 else cq.Compound.makeCompound(vals)


# ------------------------------------------------------------------ reference geometry
def load_pcb(d):
    src = d.bd["source"]
    shape = cq.importers.importStep(str(HERE.parent.parent / src["step"])).val()
    fps, _, _, _ = parse_pcb(open(HERE.parent.parent / src["pcb"]).read())
    return shape, fps


def nearest_ref(fps, bb):
    cx, cy = (bb.xmin + bb.xmax) / 2, -(bb.ymin + bb.ymax) / 2
    if bb.xlen > 150:
        return "PCB"
    return min(fps, key=lambda r: math.hypot(fps[r]["x"] - cx, fps[r]["y"] - cy))


def screws(d):
    p, z = d.p, d.z
    out = []
    for h in d.bd["mount_holes"]:
        x, y = d.m(h["x"], h["y"])
        s = E.cyl(x, y, z["head_seat"] - p.head_h, z["head_seat"], p.head_d) \
            .union(E.cyl(x, y, z["head_seat"] - 0.01, z["screw_tip"], p.screw_d))
        out.append(solid(s))
    return out


# ------------------------------------------------------------------ checks
def check_parts(parts):
    res = {}
    for k, w in parts.items():
        s = solid(w)
        bb = s.BoundingBox()
        res[k] = {"valid": s.isValid(), "solids": len(s.Solids()), "volume_mm3": round(s.Volume(), 1),
                  "bbox": [round(v, 2) for v in (bb.xmin, bb.xmax, bb.ymin, bb.ymax, bb.zmin, bb.zmax)],
                  "size": [round(bb.xlen, 2), round(bb.ylen, 2), round(bb.zlen, 2)]}
    return res


def check_interference(parts, pcb_shape, fps, near=0.25, quick=False):
    """Exact boolean intersection + minimum distance of each printed part vs every PCB solid."""
    hits, close = [], []
    for k in ("bottom", "lid", "labels"):
        part = solid(parts[k])
        pb = part.BoundingBox()
        for s in pcb_shape.Solids():
            b = s.BoundingBox()
            if b.xmax < pb.xmin - 1 or b.xmin > pb.xmax + 1 or b.ymax < pb.ymin - 1 or b.ymin > pb.ymax + 1 \
                    or b.zmax < pb.zmin - 1 or b.zmin > pb.zmax + 1:
                continue
            v = part.intersect(s).Volume()
            ref = nearest_ref(fps, b)
            if v > 1e-3:
                hits.append({"part": k, "ref": ref, "volume_mm3": round(v, 4)})
                continue
            if quick:
                continue
            dist = BRepExtrema_DistShapeShape(part.wrapped, s.wrapped)
            if dist.IsDone() and dist.Value() < near:
                close.append({"part": k, "ref": ref, "min_dist_mm": round(dist.Value(), 3)})
    return hits, close


def check_fasteners(d, parts):
    p, z = d.p, d.z
    return {
        "screw": f"{p.screw_d} x {p.screw_len} pan head self-tapping",
        "head_seat_z": round(z["head_seat"], 3), "screw_tip_z": round(z["screw_tip"], 3),
        "engagement_in_lid_post_mm": round(z["screw_tip"] - z["seam"], 2),
        "pilot_end_z": round(z["pilot_end"], 3), "lid_top_z": round(z["top"], 3),
        "material_above_pilot_mm": round(z["top"] - z["pilot_end"], 2),
        "head_recess_below_foot_seat_mm": round(z["head_seat"] - p.head_h - (z["bot"] + p.foot_recess), 2),
        "pcb_clamp": "bottom post top z=0 / lid post bottom z=seam -> exactly the PCB thickness",
    }


def check_user_access(d):
    p, z = d.p, d.z
    btn_top = max(s["top_z"] for s in d.bd["buttons"])
    return {
        "button_stem_tip_below_lid_top_mm": round(z["top"] - btn_top, 2),
        "dimple": f"d{p.dimple_d} {p.dimple_angle} deg cone, depth {(p.dimple_d - p.stem_hole_d) / 2 / math.tan(math.radians(p.dimple_angle)):.2f} mm",
        "ceiling_over_tallest_part_mm": round(z["ceil"] - d.bd["heights"]["tallest_non_button_z"], 2),
        "floor_under_lowest_tail_mm": round(d.bd["heights"]["lowest_below_pcb_z"] - z["floor_top"], 2),
        "connector_face_recess_mm": {r: round({"top": d.oy1 - (-c["face"]), "bottom": (-c["face"]) - d.oy0,
                                               "right": d.ox1 - c["face"]}[c["edge"]], 3)
                                     for r, c in d.bd["edge_connectors"].items()},
        "usbc_overmold_relief": E.usbc_relief_cut(d) is not None,
        "terminal_screw_top_to_lid_top_mm": round(z["top"] - 11.0, 2),
    }


# ------------------------------------------------------------------ exports
def nurbs(w):
    """B-spline copy of a part for STEP: offset curves of the thickened glyphs do not survive a STEP
    round trip (solids get lost on re-import); converted geometry does (verified volume + solid count)."""
    from OCP.BRepBuilderAPI import BRepBuilderAPI_NurbsConvert
    return cq.Shape.cast(BRepBuilderAPI_NurbsConvert(solid(w).wrapped, True).Shape())


def export_all(d, parts, pcb_shape, scr):
    (OUT / "step").mkdir(parents=True, exist_ok=True)
    (OUT / "stl").mkdir(parents=True, exist_ok=True)
    asm = cq.Assembly(name="usb_debug_ctrl_enclosure")
    asm.add(parts["bottom"], name="bottom", color=cq.Color(*COL["bottom"]))
    asm.add(nurbs(parts["lid"]), name="lid", color=cq.Color(*COL["lid"]))
    asm.add(nurbs(parts["labels"]), name="lid_labels", color=cq.Color(*COL["labels"]))
    asm.add(pcb_shape, name="pcb_reference_r1_2643", color=cq.Color(*COL["pcb"]))
    for i, s in enumerate(scr):
        asm.add(s, name=f"screw_3x16_{i + 1}", color=cq.Color(*COL["screw"]))
    asm.save(str(OUT / "step" / "enclosure_assembly.step"))
    for k, fn in (("bottom", "bottom"), ("lid", "lid"), ("labels", "lid_labels")):
        cq.exporters.export(cq.Workplane().add(nurbs(parts[k])), str(OUT / "step" / f"{fn}.step"))
    po = print_oriented(d, parts)
    for k, s in po.items():
        cq.exporters.export(cq.Workplane().add(s), str(OUT / "stl" / f"{k}_print.stl"), tolerance=0.01,
                            angularTolerance=0.1)
    ex = HERE / "examples" / "centauri_carbon_canvas"             # optional example: Centauri Carbon + CANVAS
    ex.mkdir(parents=True, exist_ok=True)
    write_3mf(po, ex / "benchhand_enclosure_centauri_canvas.3mf")
    gen = OUT / "generic"                                           # primary, printer-independent deliverables
    gen.mkdir(parents=True, exist_ok=True)
    write_generic_3mf(po, gen / "benchhand_enclosure.3mf")
    with zipfile.ZipFile(gen / "benchhand_enclosure_step.zip", "w", zipfile.ZIP_DEFLATED) as zf:
        for fn in ("bottom", "lid", "lid_labels"):
            zf.write(OUT / "step" / f"{fn}.step", f"{fn}.step")
    return po


def write_generic_3mf(po, path):
    """Plain 3MF (core spec + base materials, no slicer or printer settings): lid = body (black) + label inlay
    (white) as two parts of one object, bottom as a second object, all in print orientation on a 256 mm plate."""
    NS = 'xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02"'
    mats = (' <basematerials id="1">\n  <base name="PLA black (body)" displaycolor="#161616FF"/>\n'
            '  <base name="PLA white (labels)" displaycolor="#F2F2F2FF"/>\n </basematerials>\n')
    lb = _mesh_box([po["lid"], po["lid_labels"]])
    bb = _mesh_box([po["bottom"]])

    def mesh_obj(oid, name, shape, c, pindex):
        verts, tris = weld(shape)
        v = "".join(f'<vertex x="{x - c[0]:.5f}" y="{y - c[1]:.5f}" z="{z - c[2]:.5f}"/>' for x, y, z in verts)
        t = "".join(f'<triangle v1="{a}" v2="{b}" v3="{cc}"/>' for a, b, cc in tris)
        return (f' <object id="{oid}" name="{name}" type="model" pid="1" pindex="{pindex}">\n'
                f'  <mesh><vertices>{v}</vertices><triangles>{t}</triangles></mesh>\n </object>\n')
    cl = ((lb.xmin + lb.xmax) / 2, (lb.ymin + lb.ymax) / 2, lb.zmin)
    cb = ((bb.xmin + bb.xmax) / 2, (bb.ymin + bb.ymax) / 2, bb.zmin)
    res = (mats + mesh_obj(2, "lid_body", po["lid"], cl, 0) + mesh_obj(3, "lid_labels", po["lid_labels"], cl, 1)
           + ' <object id="4" name="lid" type="model">\n  <components>\n   <component objectid="2"/>\n'
           '   <component objectid="3"/>\n  </components>\n </object>\n'
           + mesh_obj(5, "bottom", po["bottom"], cb, 0))
    meta = (' <metadata name="Title">BenchHand enclosure (USB-DEBUG-CTRL r1)</metadata>\n'
            ' <metadata name="Description">Print orientation. lid = lid_body (black) + lid_labels (white, inlaid'
            ' 0.4 mm into the face on the bed: first 2 layers, assign a second filament or print the 2 layers'
            ' with a filament change). bottom: black, enable support from the build plate (screw-head pockets).'
            '</metadata>\n')
    model = (f'<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xml:lang="en-US" {NS}>\n' + meta
             + ' <resources>\n' + res + ' </resources>\n <build>\n'
             f'  <item objectid="4" transform="1 0 0 0 1 0 0 0 1 {PLATE / 2:.3f} {PLATE / 2 - 30:.3f} 0"/>\n'
             f'  <item objectid="5" transform="1 0 0 0 1 0 0 0 1 {PLATE / 2:.3f} {PLATE / 2 + 30:.3f} 0"/>\n'
             ' </build>\n</model>\n')
    ctypes = ('<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/'
              'content-types">\n <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.'
              'relationships+xml"/>\n <Default Extension="model" ContentType="application/vnd.ms-package.'
              '3dmanufacturing-3dmodel+xml"/>\n</Types>\n')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/'
            'package/2006/relationships">\n <Relationship Target="/3D/3dmodel.model" Id="rel-1" '
            'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n</Relationships>\n')
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("[Content_Types].xml", ctypes)
        zf.writestr("_rels/.rels", rels)
        zf.writestr("3D/3dmodel.model", model)


def print_oriented(d, parts):
    """Bottom as built (floor on the bed); lid + labels flipped 180 deg about X (top face on the bed)."""
    z = d.z
    b = solid(parts["bottom"]).translate(cq.Vector(0, 0, -z["bot"]))
    flip = lambda s: s.rotate(cq.Vector(0, 0, 0), cq.Vector(1, 0, 0), 180).translate(cq.Vector(0, 0, z["top"]))
    return {"bottom": b, "lid": flip(solid(parts["lid"])), "lid_labels": flip(solid(parts["labels"]))}


def weld(shape, tol=0.01):
    """Tessellate and merge coincident vertices (OCC emits per-face vertex lists) -> closed mesh."""
    verts, tris = shape.tessellate(tol, 0.1)
    key, idx, out_v = {}, [], []
    for p in verts:
        k = (round(p.x, 4), round(p.y, 4), round(p.z, 4))
        if k not in key:
            key[k] = len(out_v)
            out_v.append(k)
        idx.append(key[k])
    out_t = [(idx[a], idx[b], idx[c]) for a, b, c in tris]
    out_t = [t for t in out_t if len(set(t)) == 3]
    return out_v, out_t


def mesh_stats(shape):
    v, t = weld(shape)
    edges = {}
    for a, b, c in t:
        for e in ((a, b), (b, c), (c, a)):
            k = tuple(sorted(e))
            edges[k] = edges.get(k, 0) + 1
    return {"triangles": len(t), "open_edges": sum(1 for n in edges.values() if n == 1),
            "non_manifold_edges": sum(1 for n in edges.values() if n > 2)}


def _mesh_xml(oid, shape, offset, tol=0.01):
    verts, tris = weld(shape, tol)
    ox, oy, oz = offset
    v = "".join(f'<vertex x="{x - ox:.5f}" y="{y - oy:.5f}" z="{z - oz:.5f}"/>' for x, y, z in verts)
    t = "".join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in tris)
    return (f' <object id="{oid}" p:UUID="{uuid.uuid4()}" type="model">\n  <mesh><vertices>{v}</vertices>'
            f'<triangles>{t}</triangles></mesh>\n </object>\n')


# per-object slicer settings: the bottom is printed floor-down, its screw-head pockets (d6.6, open to the bed)
# have an annular ceiling around the d3.4 clearance hole that does not bridge -> support from the plate (D-136)
OBJECT_SUPPORT = {
    "enclosure_bottom": '    <metadata key="enable_support" value="1"/>\n'
                        '    <metadata key="support_type" value="normal(auto)"/>\n'
                        '    <metadata key="support_on_build_plate_only" value="1"/>\n',
}


class _MeshBox:
    def __init__(self, pts):
        xs, ys, zs = zip(*pts)
        self.xmin, self.xmax, self.ymin, self.ymax, self.zmin, self.zmax = min(xs), max(xs), min(ys), max(ys), \
            min(zs), max(zs)
        self.zlen = self.zmax - self.zmin


def _mesh_box(shapes, tol=0.01):
    return _MeshBox([v for sh in shapes for v in weld(sh, tol)[0]])


def write_3mf(po, path):
    """OrcaSlicer / ElegooSlicer project-style 3MF: one plate, lid = body (filament 1) + labels (filament 2)."""
    NS = ('xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" '
          'xmlns:BambuStudio="http://schemas.bambulab.com/package/2021" '
          'xmlns:p="http://schemas.microsoft.com/3dmanufacturing/production/2015/06" requiredextensions="p"')
    head = f'<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xml:lang="en-US" {NS}>\n' \
           ' <metadata name="BambuStudio:3mfVersion">1</metadata>\n'
    # object groups: (parent id, name, [(part id, name, shape, extruder)], plate position y)
    # placement from the exported mesh vertices: the OCC bounding box is not tight (it grew ~0.1 mm on the
    # label B-splines and lifted the lid off the bed -> OrcaSlicer "Failed slicing the model")
    lb = _mesh_box([po["lid"], po["lid_labels"]])
    bb = _mesh_box([po["bottom"]])
    groups = [
        (3, "enclosure_lid", [(1, "lid_body_black", po["lid"], 1), (2, "lid_labels_white", po["lid_labels"], 2)],
         lb, PLATE / 2 - 30),
        (5, "enclosure_bottom", [(4, "bottom_black", po["bottom"], 1)], bb, PLATE / 2 + 30),
    ]
    files, comps, items, cfg, asm = {}, [], [], [], []
    for gid, gname, prts, gb, py in groups:
        c = ((gb.xmin + gb.xmax) / 2, (gb.ymin + gb.ymax) / 2, (gb.zmin + gb.zmax) / 2)
        fname = f"3D/Objects/{gname}_{gid}.model"
        files[fname] = head + " <resources>\n" + "".join(_mesh_xml(pid, s, c) for pid, _, s, _ in prts) + \
            " </resources>\n <build/>\n</model>\n"
        comps.append(f'  <object id="{gid}" p:UUID="{uuid.uuid4()}" type="model">\n   <components>\n' + "".join(
            f'    <component p:path="/{fname}" objectid="{pid}" p:UUID="{uuid.uuid4()}" '
            f'transform="1 0 0 0 1 0 0 0 1 0 0 0"/>\n' for pid, _, _, _ in prts) + "   </components>\n  </object>\n")
        tr = f"1 0 0 0 1 0 0 0 1 {PLATE / 2:.3f} {py:.3f} {gb.zlen / 2:.4f}"
        items.append(f'  <item objectid="{gid}" p:UUID="{uuid.uuid4()}" transform="{tr}" printable="1"/>\n')
        cfg.append(f'  <object id="{gid}">\n    <metadata key="name" value="{gname}"/>\n'
                   f'    <metadata key="extruder" value="1"/>\n' + OBJECT_SUPPORT.get(gname, "") + "".join(
                       f'    <part id="{pid}" subtype="normal_part">\n      <metadata key="name" value="{pn}"/>\n'
                       f'      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>\n'
                       f'      <metadata key="extruder" value="{ex}"/>\n    </part>\n'
                       for pid, pn, _, ex in prts) + "  </object>\n")
        asm.append(f'   <assemble_item object_id="{gid}" instance_id="0" transform="{tr}" offset="0 0 0" />\n')
    main = (f'<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xml:lang="en-US" {NS}>\n'
            ' <metadata name="Application">BambuStudio-2.3.0</metadata>\n'
            ' <metadata name="BambuStudio:3mfVersion">1</metadata>\n'
            ' <metadata name="Title">USB-DEBUG-CTRL r1 enclosure</metadata>\n'
            ' <resources>\n' + "".join(comps) + ' </resources>\n'
            f' <build p:UUID="{uuid.uuid4()}">\n' + "".join(items) + ' </build>\n</model>\n')
    plate = ('  <plate>\n    <metadata key="plater_id" value="1"/>\n    <metadata key="plater_name" value=""/>\n'
             '    <metadata key="locked" value="false"/>\n'
             # first layer white -> black, layer 2 black -> white, then black: 3 changes, white laid first on the
             # bed (owner request D-139; verified with the OrcaSlicer 2.4.2 CLI)
             '    <metadata key="first_layer_print_sequence" value="2 1"/>\n' + "".join(
                 f'    <model_instance>\n      <metadata key="object_id" value="{g[0]}"/>\n'
                 f'      <metadata key="instance_id" value="0"/>\n'
                 f'      <metadata key="identify_id" value="{100 + g[0]}"/>\n    </model_instance>\n'
                 for g in groups) + '  </plate>\n')
    config = '<?xml version="1.0" encoding="UTF-8"?>\n<config>\n' + "".join(cfg) + plate + \
             '  <assemble>\n' + "".join(asm) + '  </assemble>\n</config>\n'
    ctypes = ('<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/'
              'content-types">\n <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.'
              'relationships+xml"/>\n <Default Extension="model" ContentType="application/vnd.ms-package.'
              '3dmanufacturing-3dmodel+xml"/>\n</Types>\n')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/'
            'package/2006/relationships">\n <Relationship Target="/3D/3dmodel.model" Id="rel-1" '
            'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n</Relationships>\n')
    mrels = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/'
             'package/2006/relationships">\n' + "".join(
                 f' <Relationship Target="/{f}" Id="rel-{i + 1}" '
                 f'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n'
                 for i, f in enumerate(files)) + '</Relationships>\n')
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("[Content_Types].xml", ctypes)
        zf.writestr("_rels/.rels", rels)
        zf.writestr("3D/3dmodel.model", main)
        zf.writestr("3D/_rels/3dmodel.model.rels", mrels)
        for f, txt in files.items():
            zf.writestr(f, txt)
        zf.writestr("Metadata/model_settings.config", config)
        zf.writestr("Metadata/project_settings.config", json.dumps(project_settings(), indent=4))


def project_settings():
    """OrcaSlicer project settings = the system presets the owner uses for CANVAS multi-colour prints (D-134/D-135):
    printer "Elegoo Centauri Carbon 0.4 nozzle", process "0.20mm Standard @Elegoo CC 0.4 nozzle", 2 x filament
    "Generic PLA @System" (black, white), on top of the owner's project file layout (slicer/), plus the two
    deliberate label overrides below."""
    t = json.load(open(HERE / "slicer" / "centauri_carbon_template.json"))
    s = t["settings"]
    for k in t["per_filament_keys"]:
        s[k] = [s[k][0], s[k][0]]
    proc = json.load(open(HERE / "slicer" / "process_020_cc04_system.json"))["values"]
    for k, v in proc.items():
        if k not in ("name", "compatible_printers", "compatible_printers_condition"):
            s[k] = v
    g = json.load(open(HERE / "slicer" / "generic_pla_system.json"))["values"]
    for k, v in g.items():
        if k in ("name", "compatible_printers", "compatible_printers_condition", "compatible_prints",
                 "compatible_prints_condition"):
            continue
        s[k] = [v[0], v[0]] if isinstance(v, list) and len(v) == 1 else v
    # printer: CANVAS auto change via M6211, CC start/end G-code; the owner's old single-colour project had manual
    # M600 changes and a 2024 start G-code (D-134). Host address is never written.
    m = json.load(open(HERE / "slicer" / "centauri_carbon_04_system.json"))["values"]
    for k, v in m.items():
        if k != "host_type":
            s[k] = v
    s["printer_settings_id"] = "Elegoo Centauri Carbon 0.4 nozzle"
    s["print_settings_id"] = "0.20mm Standard @Elegoo CC 0.4 nozzle"
    s["filament_settings_id"] = ["Generic PLA @System"] * 2
    s["inherits_group"] = ["", "", "", ""]                  # print, 2 filaments, printer: all system presets
    if "filament_ids" in s:
        s["filament_ids"] = [g.get("filament_id", "OGFL99")] * 2
    s["filament_colour"] = ["#161616", "#F2F2F2"]
    s["default_filament_colour"] = ["", ""]
    # thin label strokes in layer 1: variable-width walls and a first-layer width equal to the normal width
    s["wall_generator"] = "arachne"
    s["initial_layer_line_width"] = "0.42"
    s["different_settings_to_system"] = ["initial_layer_line_width;wall_generator", "", "", ""]
    # purge into the CANVAS chute (M6211 L[flush_length]); black -> white needs the long purge
    s["flush_volumes_matrix"] = ["0", "800", "300", "0"]
    s["flush_volumes_vector"] = ["140"] * 4
    # prime tower as in the system process (owner's frakce_* projects): free plate area behind the parts
    s["enable_prime_tower"] = "1"
    s["wipe_tower_cone_angle"] = "30"                    # owner's saved project (stabilising cone under the tower)
    s["pressure_advance"] = ["0.02", "0.02"]             # owner's saved project
    s["wipe_tower_x"] = ["165"]
    s["wipe_tower_y"] = ["200"]
    return s


def reimport_steps(parts):
    res = {}
    for k, fn in (("bottom", "bottom"), ("lid", "lid"), ("labels", "lid_labels")):
        s = cq.importers.importStep(str(OUT / "step" / f"{fn}.step")).val()
        ref = solid(parts[k])
        res[fn] = {"valid": s.isValid(), "volume_mm3": round(s.Volume(), 1),
                   "volume_matches": abs(s.Volume() - ref.Volume()) < 0.01 * ref.Volume(),
                   "solids": len(s.Solids()), "solids_expected": len(ref.Solids())}
    a = cq.importers.importStep(str(OUT / "step" / "enclosure_assembly.step")).val()
    # the KiCad board STEP itself contains 1 invalid component model -> assembly validity is reported, not gated
    res["enclosure_assembly"] = {"valid": a.isValid(), "solids": len(a.Solids()),
                                 "invalid_solids": sum(1 for x in a.Solids() if not x.isValid())}
    return res


def regen_test(bd):
    """Change two important parameters, rebuild, confirm the dependants follow."""
    out = {}
    for name, p in (("wall=2.2,dimple_d=12", E.Params(wall=2.2, dimple_d=12.0)),
                    ("stem_recess=1.5,gap=0.3", E.Params(stem_recess=1.5, gap=0.3))):
        d, parts = E.build_all(p, bd)
        r = check_parts(parts)
        out[name] = {"all_valid": all(v["valid"] for v in r.values()),
                     "bottom_size": r["bottom"]["size"], "lid_size": r["lid"]["size"]}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-previews", action="store_true")
    ap.add_argument("--no-regen-test", action="store_true")
    ap.add_argument("--quick", action="store_true", help="intersections only, skip the slow distance check")
    a = ap.parse_args()
    OUT.mkdir(exist_ok=True)
    t0 = time.time()
    bd = E.load_board()
    d, parts = E.build_all(E.Params(), bd)
    log(f"built in {time.time() - t0:.1f} s; z levels {json.dumps({k: round(v, 3) for k, v in d.z.items()})}")
    pcb_shape, fps = load_pcb(d)
    scr = screws(d)
    report = {"inputs": bd["source"], "params": E.Params().__dict__, "z_levels": d.z,
              "outer_size_mm": [round(d.ox1 - d.ox0, 2), round(d.oy1 - d.oy0, 2), round(d.z["top"] - d.z["bot"], 2)],
              "supports": E.support_points(d)}
    report["parts"] = check_parts(parts)
    report["labels"] = {"glyph_failures": E.BOLD_FAILURES,
                        "glyph_gaps_below_0.3mm": [g for g in E.TEXT_GAPS if g[2] < 0.3],
                        "element_gaps_below_0.5mm": E.label_clearance(d)}
    import label_check
    report["labels"]["raster_check_pct"] = [
        {"text": r[0], "size": r[1], "bold": r[2], "strokes_thinner_0.42": r[3], "black_gaps_narrower_0.42": r[4],
         "strokes_thinner_0.84": r[5]}
        for r in label_check.check(E.Params())]
    log("labels", json.dumps(report["labels"]))
    log("parts", json.dumps(report["parts"]))
    export_all(d, parts, pcb_shape, scr)
    report["mesh"] = {k: mesh_stats(v) for k, v in print_oriented(d, parts).items()}
    log("mesh", json.dumps(report["mesh"]))
    hits, close = check_interference(parts, pcb_shape, fps, quick=a.quick)
    sh = [{"screw": i + 1, "part": k, "vol": round(solid(parts[k]).intersect(s).Volume(), 3)}
          for i, s in enumerate(scr) for k in ("bottom", "lid")]
    report["interference"] = {"intersections": hits, "closer_than_0.25mm": close,
                              "screw_vs_parts_overlap_mm3": sh}
    log("interference", json.dumps(report["interference"]))
    report["fasteners"] = check_fasteners(d, parts)
    report["access"] = check_user_access(d)
    report["step_reimport"] = reimport_steps(parts)
    log("reimport", json.dumps(report["step_reimport"]))
    if not a.no_regen_test:
        report["regen_test"] = regen_test(bd)
        log("regen", json.dumps(report["regen_test"]))
    if not a.no_previews:
        import render
        render.all_views(d, parts, pcb_shape, scr, OUT / "previews")
    json.dump(report, open(OUT / "checks.json", "w"), indent=1, default=str)
    log(f"done in {time.time() - t0:.1f} s -> {OUT}")


if __name__ == "__main__":
    sys.exit(main())

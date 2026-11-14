# SPDX-License-Identifier: CC-BY-SA-4.0
# Copyright (c) 2026 Martin Faltus (mafaltu)
"""Offscreen VTK previews of the real CAD geometry (tessellated B-rep)."""
from pathlib import Path

import cadquery as cq
import vtk

from build import COL, solid


def _actor(shape, color, tol=0.05, opacity=1.0):
    pd = shape.toVtkPolyData(tol, 0.2, normals=True)
    m = vtk.vtkPolyDataMapper()
    m.SetInputData(pd)
    a = vtk.vtkActor()
    a.SetMapper(m)
    pr = a.GetProperty()
    pr.SetColor(*color)
    pr.SetOpacity(opacity)
    pr.SetSpecular(0.15)
    pr.SetInterpolationToPhong()
    return a


def _render(items, path, cam, size=(2000, 1100), parallel=False):
    ren = vtk.vtkRenderer()
    ren.SetBackground(0.93, 0.93, 0.95)
    for shape, color in items:
        ren.AddActor(_actor(shape, color))
    win = vtk.vtkRenderWindow()
    win.SetOffScreenRendering(1)
    win.SetSize(*size)
    win.AddRenderer(ren)
    c = ren.GetActiveCamera()
    c.SetFocalPoint(*cam["focal"])
    c.SetPosition(*cam["pos"])
    c.SetViewUp(*cam.get("up", (0, 0, 1)))
    if parallel:
        c.ParallelProjectionOn()
    ren.ResetCamera()
    if parallel and "pscale" in cam:
        c.SetParallelScale(cam["pscale"])
    elif "zoom" in cam:
        c.Zoom(cam["zoom"])
    win.Render()
    w2i = vtk.vtkWindowToImageFilter()
    w2i.SetInput(win)
    w2i.Update()
    wr = vtk.vtkPNGWriter()
    wr.SetFileName(str(path))
    wr.SetInputConnection(w2i.GetOutputPort())
    wr.Write()


def _pcb_items(pcb_shape):
    items = []
    for s in pcb_shape.Solids():
        bb = s.BoundingBox()
        items.append((s, COL["pcb"] if bb.xlen > 150 else COL["comp"]))
    return items


def _cut(items, keep_box):
    out = []
    for s, c in items:
        bb = s.BoundingBox()
        kb = keep_box.BoundingBox()
        if bb.xmax < kb.xmin or bb.xmin > kb.xmax or bb.ymax < kb.ymin or bb.ymin > kb.ymax:
            continue
        r = s.intersect(keep_box)
        if r.Volume() > 1e-6:
            out.append((r, c))
    return out


def all_views(d, parts, pcb_shape, scr, out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    B, L, T = solid(parts["bottom"]), solid(parts["lid"]), solid(parts["labels"])
    cx, cy = (d.ox0 + d.ox1) / 2, (d.oy0 + d.oy1) / 2
    closed = [(B, COL["bottom"]), (L, COL["lid"]), (T, COL["labels"])]
    pcb = _pcb_items(pcb_shape)
    f = (cx, cy, 5)
    _render(closed, out_dir / "iso_front.png", {"focal": f, "pos": (cx - 120, cy - 260, 210), "zoom": 1.35})
    _render(closed, out_dir / "iso_back.png", {"focal": f, "pos": (cx + 140, cy + 260, 200), "zoom": 1.35})
    _render(closed, out_dir / "top.png", {"focal": f, "pos": (cx, cy, 400), "up": (0, 1, 0), "zoom": 1.25},
            parallel=True)
    zc = (d.z["bot"] + d.z["top"]) / 2
    withpcb = closed + _pcb_items(pcb_shape)
    _render(withpcb, out_dir / "front_lower_edge.png", {"focal": (cx, cy, zc), "pos": (cx, cy - 400, zc),
                                                        "pscale": 16}, size=(2600, 440), parallel=True)
    _render(withpcb, out_dir / "back_upper_edge.png", {"focal": (cx, cy, zc), "pos": (cx, cy + 400, zc),
                                                       "pscale": 16}, size=(2600, 440), parallel=True)
    _render(withpcb, out_dir / "right_end.png", {"focal": (d.ox1, cy, zc), "pos": (d.ox1 + 300, cy, zc),
                                                 "pscale": 16}, size=(1000, 640), parallel=True)
    _render([(B, COL["bottom"])], out_dir / "underside.png",
            {"focal": f, "pos": (cx - 60, cy - 160, -330), "zoom": 1.35})
    # interior: bottom + PCB + screws, lid removed
    _render([(B, COL["bottom"])] + pcb + [(s, COL["screw"]) for s in scr], out_dir / "interior_with_pcb.png",
            {"focal": f, "pos": (cx - 120, cy - 260, 210), "zoom": 1.35})
    _render([(B, COL["bottom"])] + [(s, COL["screw"]) for s in scr], out_dir / "interior_bottom.png",
            {"focal": f, "pos": (cx - 120, cy - 220, 260), "zoom": 1.35})
    # lid underside (flipped view)
    _render([(L, COL["lid"]), (T, COL["labels"])], out_dir / "lid_underside.png",
            {"focal": f, "pos": (cx - 100, cy - 200, -300), "zoom": 1.35})
    # exploded
    ex = 28
    expl = [(B, COL["bottom"])] + [(s.translate(cq.Vector(0, 0, ex)), c) for s, c in pcb] + \
           [(L.translate(cq.Vector(0, 0, 2 * ex)), COL["lid"]), (T.translate(cq.Vector(0, 0, 2 * ex)), COL["labels"])]
    _render(expl, out_dir / "exploded.png", {"focal": (cx, cy, 30), "pos": (cx - 130, cy - 300, 200), "zoom": 1.3},
            size=(2000, 1400))
    # sections (keep x < plane, look from +x)
    every = closed + pcb + [(s, COL["screw"]) for s in scr]
    for name, x in (("section_relay1_button", d.bd["buttons"][0]["x"]),
                    ("section_usb_passthrough", (d.bd["edge_connectors"]["J401"]["a0"] +
                                                 d.bd["edge_connectors"]["J401"]["a1"]) / 2),
                    ("section_screw_post", d.bd["mount_holes"][0]["x"])):
        if name == "section_relay1_button":
            x = [b for b in d.bd["buttons"] if b["ref"] == "SW101"][0]["x"]
        keep = cq.Solid.makeBox(x - (d.ox0 - 5), 80, 60, cq.Vector(d.ox0 - 5, d.oy0 - 10, -20))
        _render(_cut(every, keep), out_dir / f"{name}.png",
                {"focal": (x, cy, 5), "pos": (x + 300, cy, 5), "pscale": 14}, size=(1500, 700), parallel=True)

# SPDX-License-Identifier: CC-BY-SA-4.0
# Copyright (c) 2026 Martin Faltus (mafaltu)
"""Hero renders (README): enclosure from the CAD model + the real, coloured board from a KiCad GLB export.

The board GLB comes from the released PCB (KiCad 10 kicad-cli, headless):
    kicad-cli pcb export glb --force --include-silkscreen --include-soldermask --include-pads --include-tracks \
        --include-zones -o board.glb cad/usb_debug_ctrl.kicad_pcb
Usage:
    .venv/bin/python render_hero.py <board.glb> <out_dir>     -> benchhand_closed.png, benchhand_exploded.png

GLB frame: metres, Y up, Z = KiCad y (down). Model frame (enclosure CAD): mm, X = KiCad x, Y = -KiCad y, Z up,
Z = 0 at the PCB bottom -> X = 1000 x, Y = -1000 z, Z = 1000 y.

Render-only adjustments (the PCB data is untouched):
- the six 3 mm LEDs are lifted on their leads so the domes stand LED_PROUD above the lid top (as assembled, the KiCad
  model has them seated at the board); the leads are drawn as straight wires down to the board;
- solder mask shown in a JLC-like green, pads as HASL (silver) instead of the KiCad default dark green / gold;
- relays K201-K205 (project model = plain grey box with the datasheet outline 10 x 6.5 mm) get a white housing and
  metal pins (HFD4 datasheet photo); the USB-B J401 shell is shown as metal (Molex 67068-8000: black resin, metal shell).
"""
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
import trimesh
import vtk
from PIL import Image
from scipy import ndimage
from vtk.util.numpy_support import numpy_to_vtk, numpy_to_vtkIdTypeArray, vtk_to_numpy

import enclosure as E
from build import COL, solid

LED_PROUD = 0.5          # LED dome top above the lid top face (mm)
MASK = (0.10, 0.40, 0.20)
HASL = (0.80, 0.80, 0.82)
BG = (0.96, 0.96, 0.97)
RELAY_BODY = (0.92, 0.92, 0.89)   # HFD4 white housing (datasheets/HFD4.pdf p.1 photo)
METAL = (0.80, 0.80, 0.82)


def _glb_meshes(path):
    """[(ref, rgb, opacity, vertices (model frame, mm), faces)] for every geometry node."""
    sc = trimesh.load(path, force="scene")
    g = sc.graph
    out = []
    for node in g.nodes_geometry:
        T, gname = g[node]
        m = sc.geometry[gname]
        n, ref = node, None
        while True:                                   # first named ancestor = reference designator / board
            p = g.transforms.parents.get(n)
            if p is None or p == g.base_frame:
                break
            if ref is None and not p.startswith("=>"):
                ref = p                               # nearest named ancestor
            n = p
        v = trimesh.transform_points(m.vertices, T) * 1000.0
        v = np.column_stack([v[:, 0], -v[:, 2], v[:, 1]])
        mat = getattr(m.visual, "material", None)
        col = getattr(mat, "baseColorFactor", None) if mat is not None else None
        if col is None:
            col = np.array(m.visual.main_color, dtype=float)
        col = np.asarray(col, dtype=float)
        if col.max() > 1.0:
            col = col / 255.0
        rgb, alpha = tuple(col[:3]), float(col[3]) if len(col) > 3 else 1.0
        out.append((ref or node, rgb, alpha, v, np.asarray(m.faces)))
    return out


def _poly(v, f):
    pd = vtk.vtkPolyData()
    pts = vtk.vtkPoints()
    pts.SetData(numpy_to_vtk(np.ascontiguousarray(v, dtype=np.float64), deep=True))
    pd.SetPoints(pts)
    cells = np.hstack([np.full((len(f), 1), 3), f]).astype(np.int64).ravel()
    ca = vtk.vtkCellArray()
    ca.SetCells(len(f), numpy_to_vtkIdTypeArray(cells, deep=True))
    pd.SetPolys(ca)
    nf = vtk.vtkPolyDataNormals()
    nf.SetInputData(pd)
    nf.SetFeatureAngle(35)
    nf.Update()
    return nf.GetOutput()


def _actor_pd(pd, rgb, opacity=1.0, spec=0.25):
    m = vtk.vtkPolyDataMapper()
    m.SetInputData(pd)
    a = vtk.vtkActor()
    a.SetMapper(m)
    p = a.GetProperty()
    p.SetColor(*rgb)
    p.SetOpacity(opacity)
    p.SetSpecular(spec)
    p.SetSpecularPower(30)
    p.SetInterpolationToPhong()
    return a


def board_actors(glb, lid_top, dz_board=0.0):
    """Board actors grouped by colour; LEDs lifted to the lid."""
    meshes = _glb_meshes(glb)
    groups = defaultdict(lambda: ([], [], 0))
    leds = defaultdict(list)
    for ref, rgb, alpha, v, f in meshes:
        if ref.startswith("LED"):
            leds[ref].append((rgb, alpha, v, f))
            continue
        if ref.startswith("K2"):                      # HFD4 relay: white housing (datasheet photo), metal pins
            cz = v[f].mean(axis=1)[:, 2]
            body = cz > 1.6 + 0.3 + 0.05              # housing sits 0.3 mm above the board (datasheet outline)
            for sel, col in ((body, RELAY_BODY), (~body, METAL)):
                if sel.any():
                    used = np.unique(f[sel])
                    remap = -np.ones(len(v), dtype=np.int64)
                    remap[used] = np.arange(len(used))
                    key = (col, 1.0)
                    vs, fs, n = groups[key]
                    vs.append(v[used] + [0, 0, dz_board])
                    fs.append(remap[f[sel]] + n)
                    groups[key] = (vs, fs, n + len(used))
            continue
        if ref == "J401" and abs(rgb[0] - 0.45) < 0.02 and abs(rgb[2] - 0.45) < 0.02:
            rgb = METAL                               # Molex 67068-8000 shell (black resin stays black)
        if ref == "0":                                # board: body, mask, silk, copper
            if rgb[1] > rgb[0] and rgb[1] > rgb[2] and max(rgb) < 0.3:
                rgb, alpha = MASK, 1.0
            elif rgb[0] > 0.6 and rgb[2] < 0.15:      # gold-coloured copper -> HASL
                rgb = HASL
        key = (tuple(round(c, 3) for c in rgb), round(alpha, 2))
        vs, fs, n = groups[key]
        vs.append(v + [0, 0, dz_board])
        fs.append(f + n)
        groups[key] = (vs, fs, n + len(v))
    actors = []
    for (rgb, alpha), (vs, fs, _) in groups.items():
        actors.append(_actor_pd(_poly(np.vstack(vs), np.vstack(fs)), rgb, alpha))
    for ref, parts in leds.items():
        allv = np.vstack([p[2] for p in parts])
        lift = lid_top + LED_PROUD - allv[:, 2].max()
        body_bottom = None
        for rgb, alpha, v, f in parts:
            vv = v + [0, 0, lift + dz_board]
            if max(rgb) > 0.5 and abs(rgb[0] - rgb[1]) < 0.1 and abs(rgb[1] - rgb[2]) < 0.1:
                col, op = (0.75, 0.75, 0.77), 1.0             # leads (metal)
            else:
                col, op = (0.20, 0.85, 0.25), 0.92            # L-934LGD green diffused body
                zb = vv[:, 2].min()
                body_bottom = zb if body_bottom is None else min(body_bottom, zb)
            actors.append(_actor_pd(_poly(vv, f), col, op, spec=0.5))
        # straight leads from the board top to the lifted lead ends (the KiCad leads end below the board)
        low = allv[allv[:, 2] < 0.0]
        if len(low):
            xs = low[:, :2]
            ax = int(np.argmax(np.ptp(xs, axis=0)))           # the two leads lie along this axis
            c0 = xs[xs[:, ax] <= np.median(xs[:, ax])].mean(0)
            c1 = xs[xs[:, ax] > np.median(xs[:, ax])].mean(0)
            for c in (c0, c1):
                cyl = vtk.vtkCylinderSource()
                cyl.SetRadius(0.25)
                z0, z1 = 1.6 + dz_board, allv[:, 2].min() + lift + dz_board + 0.5
                cyl.SetHeight(z1 - z0)
                cyl.SetResolution(16)
                tf = vtk.vtkTransform()
                tf.Translate(c[0], c[1], (z0 + z1) / 2)
                tf.RotateX(90)
                tp = vtk.vtkTransformPolyDataFilter()
                tp.SetTransform(tf)
                tp.SetInputConnection(cyl.GetOutputPort())
                tp.Update()
                actors.append(_actor_pd(tp.GetOutput(), (0.75, 0.75, 0.77), 1.0, spec=0.5))
    return actors


def cad_actor(shape, rgb, dz=0.0, tol=0.01, ang=0.05):
    pd = shape.translate((0, 0, dz)).toVtkPolyData(tol, ang, normals=True)
    return _actor_pd(pd, rgb, 1.0, spec=0.15)


def _grab(win, ren, bg):
    ren.SetBackground(*bg)
    win.Render()
    w2i = vtk.vtkWindowToImageFilter()
    w2i.SetInput(win)
    w2i.SetInputBufferTypeToRGB()
    w2i.ReadFrontBufferOff()
    w2i.Update()
    img = w2i.GetOutput()
    w, h, _ = img.GetDimensions()
    arr = vtk_to_numpy(img.GetPointData().GetScalars()).reshape(h, w, 3).astype(np.float64) / 255.0
    return arr[::-1]                                   # VTK rows start at the bottom


def render(actors, path, cam, size=(3600, 2025), out_w=2400, margin=0.02, rim=(205, 208, 214), rim_px=3):
    """Transparent RGBA PNG: the scene is rendered on white and on black, alpha = 1 - (white - black), colour =
    black render / alpha (exact matting of the anti-aliased edges). Cropped to the model + margin, scaled to out_w,
    with a thin light outline outside the silhouette so the black enclosure stays visible on dark pages."""
    ren = vtk.vtkRenderer()
    for a in actors:
        ren.AddActor(a)
    kit = vtk.vtkLightKit()
    kit.SetKeyLightIntensity(0.85)
    kit.AddLightsToRenderer(ren)
    ren.SetUseFXAA(True)
    win = vtk.vtkRenderWindow()
    win.SetOffScreenRendering(1)
    win.SetMultiSamples(0)
    win.SetSize(*size)
    win.AddRenderer(ren)
    c = ren.GetActiveCamera()
    c.SetFocalPoint(*cam["focal"])
    c.SetPosition(*cam["pos"])
    c.SetViewUp(0, 0, 1)
    c.SetViewAngle(cam.get("angle", 20))
    ren.ResetCamera()
    c.Zoom(cam.get("zoom", 1.0))
    on_w = _grab(win, ren, (1, 1, 1))
    on_b = _grab(win, ren, (0, 0, 0))
    alpha = np.clip(1.0 - (on_w - on_b).mean(axis=2), 0.0, 1.0)
    rgb = np.where(alpha[..., None] > 1e-3, on_b / np.maximum(alpha[..., None], 1e-3), 0.0)
    rgb = np.clip(rgb, 0.0, 1.0)
    ys, xs = np.nonzero(alpha > 0.02)
    m = int(margin * (xs.max() - xs.min()))
    y0, y1 = max(ys.min() - m, 0), min(ys.max() + m + 1, alpha.shape[0])
    x0, x1 = max(xs.min() - m, 0), min(xs.max() + m + 1, alpha.shape[1])
    rgba = np.dstack([rgb, alpha])[y0:y1, x0:x1]
    img = Image.fromarray((rgba * 255 + 0.5).astype(np.uint8), "RGBA")
    scale = out_w / img.width
    img = img.resize((out_w, round(img.height * scale)), Image.LANCZOS)
    if rim_px:                                         # light outline just outside the silhouette
        a = np.asarray(img)[..., 3].astype(np.float64) / 255.0
        grown = ndimage.maximum_filter(a, size=2 * rim_px + 1)
        ring = np.clip(grown - a, 0.0, 1.0) * 0.9
        base = np.asarray(img).astype(np.float64) / 255.0
        out_a = base[..., 3] + ring * (1.0 - base[..., 3])
        col = np.array(rim, dtype=np.float64) / 255.0
        out_rgb = (base[..., :3] * base[..., 3:4] + col * (ring * (1.0 - base[..., 3]))[..., None]) \
            / np.maximum(out_a[..., None], 1e-6)
        img = Image.fromarray((np.dstack([np.clip(out_rgb, 0, 1), out_a]) * 255 + 0.5).astype(np.uint8), "RGBA")
    img.save(path)
    print("saved", path, img.size)


def main(glb, out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    d, parts = E.build_all()
    B, L, T = solid(parts["bottom"]), solid(parts["lid"]), solid(parts["labels"])
    lid_top = d.z["top"]
    lid_rgb, bot_rgb, lab_rgb = (0.10, 0.10, 0.11), (0.12, 0.12, 0.13), (0.95, 0.95, 0.93)
    cx, cy = (d.ox0 + d.ox1) / 2, (d.oy0 + d.oy1) / 2

    closed = [cad_actor(B, bot_rgb), cad_actor(L, lid_rgb), cad_actor(T, lab_rgb)] + board_actors(glb, lid_top)
    render(closed, out_dir / "benchhand_closed.png",
           {"focal": (cx, cy, 4), "pos": (cx - 140, cy - 300, 230), "zoom": 1.6})

    up, down = 34.0, -26.0
    expl = [cad_actor(B, bot_rgb, down), cad_actor(L, lid_rgb, up), cad_actor(T, lab_rgb, up)] \
        + board_actors(glb, lid_top + 0.0, 0.0)
    # in the exploded view the LEDs stay at their assembled height relative to the board
    render(expl, out_dir / "benchhand_exploded.png",
           {"focal": (cx, cy, 4), "pos": (cx - 150, cy - 330, 260), "zoom": 1.35})


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])

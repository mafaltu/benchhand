# SPDX-License-Identifier: CC-BY-SA-4.0
# Copyright (c) 2026 Martin Faltus (mafaltu)
"""Raster check of the label texts: white strokes and black gaps (inside and between letters).

Each text is rasterised at 50 px/mm from its real CAD faces. A feature narrower than `w` is detected by a
morphological opening with a disc of diameter w: white pixels removed by opening the white = strokes thinner
than w; black pixels removed by opening the black = gaps (E arms, R bowl, letter spacing) narrower than w.
Usage: .venv/bin/python label_check.py   (prints a table, returns the worst values)
"""
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

import enclosure as E
import graphics

PX = 50.0     # pixels per mm


def raster(shape_wp, pad=1.0):
    faces = shape_wp.faces("<Z").vals()
    bb = shape_wp.val().BoundingBox()
    w, h = int((bb.xlen + 2 * pad) * PX), int((bb.ylen + 2 * pad) * PX)
    img = Image.new("1", (w, h), 0)
    dr = ImageDraw.Draw(img)
    for f in faces:
        verts, tris = f.tessellate(0.005, 0.05)
        pts = [((v.x - bb.xmin + pad) * PX, (bb.ymax + pad - v.y) * PX) for v in verts]
        for a, b, c in tris:
            dr.polygon([pts[a], pts[b], pts[c]], fill=1)
    return np.array(img, dtype=bool)


def disc(d_mm):
    r = d_mm * PX / 2
    n = int(np.ceil(r))
    y, x = np.ogrid[-n:n + 1, -n:n + 1]
    return x * x + y * y <= r * r


def narrow_fraction(mask, d_mm):
    """Fraction of the mask that disappears in an opening with a disc of diameter d."""
    opened = ndimage.binary_opening(mask, structure=disc(d_mm))
    lost = mask & ~opened
    return lost.sum() / max(mask.sum(), 1), lost


def check(p=None, white_min=0.42, black_min=0.42):
    p = p or E.Params()
    bd = E.load_board()
    rows = []
    seen = set()
    for g in graphics.build(bd, 190.8):
        if g[0] != "text" or (g[3], g[4]) in seen:
            continue
        seen.add((g[3], g[4]))
        wp = E._text(g[3], g[4], p.font, E.bold_for(g[4], p))
        m = raster(wp)
        inner = ndimage.binary_fill_holes(ndimage.binary_dilation(m, structure=disc(1.2)))  # text envelope
        black = inner & ~m
        fw, _ = narrow_fraction(m, white_min)
        fb, _ = narrow_fraction(black, black_min)
        f2, _ = narrow_fraction(m, 2 * white_min)      # thinner than 2 nozzle passes
        rows.append((g[3], g[4], round(E.bold_for(g[4], p), 3), round(100 * fw, 1), round(100 * fb, 1),
                     round(100 * f2, 1)))
    return rows


if __name__ == "__main__":
    print(f"{'text':14s} size bold  thin-white% narrow-black% white<2-passes%")
    for r in check():
        print(f"{r[0]:14s} {r[1]:4} {r[2]:5} {r[3]:8} {r[4]:10} {r[5]:10}")

"""Smooth the stair-stepped top outlines (mountain ridges, dunes) of Defender 2000's backdrop pictures in a high-res
pack (tools/d2k_art_pack.py runs it on the pack; also usable alone).

  python -I tools/d2k_ridges.py <src dir> <hi dir> [--only 216,217] [--detail 0.5] [--compare out.png] [--dry]

A backdrop picture's outline at 1x is runs of equal height (the art was rendered without anti-aliasing). The new
outline goes in straight lines through the middles of neighbouring runs that differ by at most MAX_STEP pixels
(staircases); bigger jumps (cacti, rocks, a cave) stay vertical, and the picture's ends keep their heights (tiles meet
their neighbours). --detail adds a little rocky roughness (pixels at 1x, 0 at the ends). The 4x picture then gets
that outline with an anti-aliased edge; its colours above the old edge are already there (the upscale input had
colours spread into see-through pixels).
Which pictures: at least 128 wide and 24 tall, standing on the ground (bottom row solid), the outline over every
column with sky above part of it, at least 12 staircase steps and at most a quarter as many big jumps (city skylines
and logos stay as they are).
"""
import argparse
import math
import os
import sys

import numpy as np
from PIL import Image

MAX_STEP = 3


def outline(alpha):
    """Top row of each column's first opaque pixel (-1: none)."""
    has = alpha.any(axis=0)
    return np.where(has, alpha.argmax(axis=0), -1)


def is_backdrop(alpha, top):
    h, w = alpha.shape
    if w < 128 or h < 24 or (top < 0).any() or top.max() == 0 or alpha[-1].mean() < 0.95:
        return False   # (a backdrop stands on the ground: its bottom row is solid; logos, sprites float)
    d = np.abs(np.diff(top))
    steps, big = int(((d >= 1) & (d <= MAX_STEP)).sum()), int((d > MAX_STEP).sum())
    return steps >= 12 and big * 4 <= steps   # (a skyline is mostly big jumps: roofs stay square)


def smooth_outline(top, scale, detail, seed):
    """The new outline at scale x (float rows, per column of the big picture)."""
    w = len(top)
    runs = []   # (x0, x1, height)
    x0 = 0
    for x in range(1, w + 1):
        if x == w or top[x] != top[x0]:
            runs.append((x0, x, float(top[x0])))
            x0 = x
    # Keyframes in 1x units: (x, y); a big jump is a vertical edge (two keys at the same x).
    keys = [(0.0, runs[0][2])]
    for i, (a, b, y) in enumerate(runs):
        mid = (a + b) / 2
        keys.append((mid, y))
        if i + 1 < len(runs):
            ny = runs[i + 1][2]
            if abs(ny - y) > MAX_STEP:
                keys.append((float(b), y))
                keys.append((float(b), ny))
    keys.append((float(w), runs[-1][2]))
    kx = np.array([k[0] for k in keys])
    ky = np.array([k[1] for k in keys])
    xs = (np.arange(w * scale) + 0.5) / scale
    # Interpolate (np.interp needs increasing x: vertical edges get the left key below b, the right one at b).
    out = np.empty_like(xs)
    j = 0
    for i, x in enumerate(xs):
        while j + 1 < len(kx) - 1 and kx[j + 1] <= x:
            j += 1
        x0, x1, y0, y1 = kx[j], kx[j + 1], ky[j], ky[j + 1]
        out[i] = y0 if x1 <= x0 else y0 + (y1 - y0) * (x - x0) / (x1 - x0)
    if detail > 0:
        rng = np.random.default_rng(seed)
        rough = np.zeros_like(xs)
        for octave in range(4):
            n = 4 << octave   # waves across the picture
            amp = detail / (1.7 ** octave)
            for _ in range(2):
                k = n + rng.integers(0, n)
                rough += amp * 0.5 * np.sin(2 * math.pi * k * xs / w + rng.uniform(0, 2 * math.pi))
        rough *= np.sin(math.pi * xs / w) ** 2   # 0 at the ends
        out = out + rough
    return out * scale


def process(src_path, hi_path, detail, dry):
    src = np.array(Image.open(src_path).convert('RGBA'))
    alpha = src[..., 3] > 0
    top = outline(alpha)
    if not is_backdrop(alpha, top):
        return None
    hi = np.array(Image.open(hi_path).convert('RGBA'))
    scale = hi.shape[1] // src.shape[1]
    seed = int(os.path.basename(src_path).split('.')[0])
    edge = smooth_outline(top, scale, detail, seed)   # rows in the big picture
    old = np.repeat(top, scale) * scale                # the old edge (big picture rows)
    new = hi.copy()
    rows = np.arange(hi.shape[0])[:, None]
    # Coverage of each pixel below the new edge (anti-aliased over one big pixel).
    cov = np.clip(rows + 1 - edge[None, :], 0, 1)
    # Change only the band around the edge (the rest keeps the upscale's mask: holes, inner shapes).
    lo = np.minimum(np.floor(edge), old)[None, :] - 2
    hi_row = np.maximum(np.ceil(edge), old)[None, :] + scale
    band = (rows >= lo) & (rows <= hi_row)
    a = new[..., 3].astype(np.float32)
    a = np.where(band, cov * 255.0, a)
    new[..., 3] = np.clip(a + 0.5, 0, 255).astype(np.uint8)
    if not dry:
        Image.fromarray(new, 'RGBA').save(hi_path)
    return hi, new


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('hi')
    ap.add_argument('--only', default='')
    ap.add_argument('--detail', type=float, default=0.0)
    ap.add_argument('--compare', default='')
    ap.add_argument('--dry', action='store_true')
    a = ap.parse_args()
    names = sorted((f for f in os.listdir(a.src) if f.endswith('.png')), key=lambda f: int(f[:-4]))
    if a.only:
        keep = set(a.only.split(','))
        names = [f for f in names if f[:-4] in keep]
    done = []
    for f in names:
        r = process(os.path.join(a.src, f), os.path.join(a.hi, f), a.detail, a.dry)
        if r:
            done.append((f, r))
    print(f'ridges smoothed: {len(done)} pictures ({", ".join(f[:-4] for f, _ in done)})')
    if a.compare and done:
        tiles = []
        for f, (old, new) in done[:8]:
            h, w = old.shape[:2]
            crop = slice(0, min(h, 160))
            t = Image.new('RGB', (w, 2 * (crop.stop) + 6), (40, 60, 90))
            for i, im in enumerate((old, new)):
                bg = Image.new('RGBA', (w, crop.stop), (90, 110, 140, 255))
                bg.alpha_composite(Image.fromarray(np.ascontiguousarray(im[crop]), 'RGBA'))
                t.paste(bg.convert('RGB'), (0, i * (crop.stop + 6)))
            tiles.append(t)
        width = max(t.width for t in tiles)
        sheet = Image.new('RGB', (width, sum(t.height + 10 for t in tiles)), (16, 16, 20))
        y = 0
        for t in tiles:
            sheet.paste(t, (0, y))
            y += t.height + 10
        sheet.save(a.compare)
        print(a.compare)


if __name__ == '__main__':
    sys.exit(main())

"""Claw feel comparison: the same stick script on several games / modes, displacement after each stick onset.

  python tools/claw_compare.py --script logs/claw/script.txt --out logs/claw/compare.png
      --txk logs/claw/txk/run2.txt:2000  --t3k "momentum=logs/claw/t3k_mo_run.txt:3710" --t3k "lr=logs/claw/t3k_lr_run.txt:3710"

Each source is <log>:<frame of script offset 0>. TxK logs (txk.exe --peek) give the claw in half lanes (24-lane web);
T3K logs (t3k.exe --clawlog) the position in lanes (16-lane web here). Displacement is signed so that the stick's
direction is positive, in lanes, over the 150 frames after each onset.
"""
import argparse
import sys
import os

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from claw_fit import read_txk  # noqa: E402


def read_t3k(path, lanes=16):
    rows, prev, off = [], None, 0.0
    for line in open(path):
        p = line.split()
        f, sx, pos = int(p[0]), float(p[1]), float(p[2])
        if prev is not None:
            d = pos - prev
            if d > lanes / 2:
                off -= lanes
            elif d < -lanes / 2:
                off += lanes
        prev = pos
        rows.append((f, sx, pos + off))
    return np.array(rows)


COLOURS = [(220, 60, 40), (40, 110, 220), (40, 160, 70), (160, 80, 200)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--script', required=True)
    ap.add_argument('--txk', action='append', default=[])
    ap.add_argument('--t3k', action='append', default=[])
    ap.add_argument('--out', required=True)
    ap.add_argument('--span', type=int, default=150)
    a = ap.parse_args()
    script = [tuple(float(x) for x in l.split()) for l in open(a.script) if l.strip() and not l.startswith('#')]
    sources = []   # (name, frames array, position array (sign: + = TxK lane decreasing), start frame, sign)
    for spec in a.txk:
        path, start = spec.rsplit(':', 1)
        d = read_txk(path, 24)
        sources.append(('TxK', d[:, 0], d[:, 2] + 0.25, int(start), -1.0))   # stick right = lanes decreasing
    for spec in a.t3k:
        name, rest = spec.split('=', 1)
        path, start = rest.rsplit(':', 1)
        d = read_t3k(path)
        sources.append(('T3K ' + name, d[:, 0], d[:, 2], int(start), 1.0))
    W, H, cols = 460, 230, 3
    n = len(script)
    rows = (n + cols - 1) // cols
    img = Image.new('RGB', (W * cols, H * rows + 30), (255, 255, 255))
    dr = ImageDraw.Draw(img)
    for k, (name, *_rest) in enumerate(sources):
        dr.rectangle([10 + 150 * k, 8, 30 + 150 * k, 20], fill=COLOURS[k % 4])
        dr.text((36 + 150 * k, 8), name, fill=(0, 0, 0))
    for i, (off, x, ln) in enumerate(script):
        ox, oy = (i % cols) * W, 30 + (i // cols) * H
        curves = []
        for name, fr, pos, start, sgn in sources:
            f0 = start + int(off)
            sel = (fr >= f0 - 5) & (fr <= f0 + a.span)
            if sel.sum() < 10:
                curves.append(None)
                continue
            base = pos[np.searchsorted(fr, f0)]
            disp = (pos[sel] - base) * sgn * (1 if x > 0 else -1)
            curves.append((fr[sel] - f0, disp))
        vals = np.concatenate([c[1] for c in curves if c is not None] + [np.array([0.0, 1.0])])
        lo, hi = min(vals.min(), -0.5), vals.max() + 0.5
        px = lambda t: ox + 40 + (t + 5) * (W - 60) / (a.span + 5)
        py = lambda v: oy + H - 25 - (v - lo) * (H - 50) / (hi - lo)
        dr.rectangle([px(0), oy + 18, px(ln), oy + 24], fill=(200, 200, 200))
        for lane in range(int(np.floor(lo)), int(np.ceil(hi)) + 1):
            dr.line([(px(-5), py(lane)), (px(a.span), py(lane))], fill=(235, 235, 235))
            dr.text((ox + 4, py(lane) - 6), f'{lane}', fill=(150, 150, 150))
        for k, c in enumerate(curves):
            if c is None:
                continue
            t, d = c
            dr.line([(px(tt), py(dd)) for tt, dd in zip(t, d)], fill=COLOURS[k % 4], width=2)
        dr.text((ox + 40, oy + 4), f'stick {x:+.2f} for {int(ln)} frames (lanes moved, frames from the push)',
                fill=(0, 0, 0))
        dr.rectangle([ox, oy, ox + W - 1, oy + H - 1], outline=(210, 210, 210))
    img.save(a.out)


if __name__ == '__main__':
    main()

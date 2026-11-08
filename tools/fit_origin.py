"""Fits the GPU-screen -> emulated-picture mapping (x' = sx*x + ox, y' = y + oy) by maximising how many points
sampled along our projected lines land on lit pixels of the emulated frame.
Usage: python -I tools/fit_origin.py logs/rm0.bmp [more shots...] (reads <shot>.lines and <shot>.orig.bmp)."""
import sys
import numpy as np
from PIL import Image

pts_all, imgs = [], []
for shot in sys.argv[1:]:
    img = np.asarray(Image.open(shot + '.orig.bmp').convert('RGB')).astype(int)
    lit = img.sum(axis=2) > 60
    pts = []
    for line in open(shot + '.lines'):
        x0, y0, x1, y1 = map(float, line.split())
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for t in np.linspace(0, 1, n + 1):
            pts.append((x0 + (x1 - x0) * t, y0 + (y1 - y0) * t))
    pts_all.append(np.array(pts))
    imgs.append(lit)

def score(sx, ox, oy):
    s = 0
    for pts, lit in zip(pts_all, imgs):
        x = np.round(pts[:, 0] * sx + ox).astype(int)
        y = np.round(pts[:, 1] + oy).astype(int)
        ok = (x >= 0) & (y >= 0) & (x < lit.shape[1]) & (y < lit.shape[0])
        s += lit[y[ok], x[ok]].sum() / max(1, len(x))
    return s / len(imgs)

best = None
for sx in np.arange(0.70, 1.051, 0.01):
    for ox in np.arange(-60, 40, 1.0):
        for oy in np.arange(-20, 21, 1.0):
            sc = score(sx, ox, oy)
            if best is None or sc > best[0]:
                best = (sc, sx, ox, oy)
sc, sx, ox, oy = best
for dsx in np.arange(-0.01, 0.0101, 0.001):
    for dox in np.arange(-1, 1.01, 0.25):
        for doy in np.arange(-1, 1.01, 0.25):
            s2 = score(sx + dsx, ox + dox, oy + doy)
            if s2 > best[0]:
                best = (s2, sx + dsx, ox + dox, oy + doy)
print('hit rate %.3f  sx %.4f  ox %.2f  oy %.2f' % best)

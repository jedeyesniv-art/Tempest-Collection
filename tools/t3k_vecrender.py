"""Tempest 3000 native renderer prototype: the vector shapes (entry type 00710001) of one picture, redrawn from the
display list at any scale - the reference for the GPU version.

python -I tools/t3k_vecrender.py <entrylog> <picture (flip count) | 'last'> <out.png> [scale] [emulated shot.bmp]

<entrylog>: NUANCE_ENTRYLOG=<f> NUANCE_ENTRYDATA=1 (each entry's words + the shape streams / palettes MPE0 read for it).
Draws every 00710001 entry of that picture in list order onto black: shape stream commands (806A3F50's table), the
bit-exact segment transform (t3k_vecmodel.py), then the line raster model: coverage = peak x (1 - d / R), d = distance
from the sample point (pixel corner in 360x240 units) to the segment, blend dest + (colour - dest) x coverage in YCrCb,
colour A at the segment's current vertex -> B at the previous one. Output in RGB (Nuance's BT.601 conversion); with a
shot, the emulated picture goes on the left.
"""
import collections
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import t3k_vecmodel as vm  # noqa: E402

RADIUS_PER_WIDTH = 0.97   # fitted on width byte 2 (t3k_linefit.py): zero coverage at 1.94 px
Z_PEAK = 0.00888          # coverage on the line per unit of the colour's 4th byte (0.553 at $3F)


def parse(path, want):
    """Entries (in fetch order) of picture `want` read by MPE0, with a memory map of the data it read."""
    pics = collections.OrderedDict()
    for line in open(path):
        t = line.split()
        if len(t) < 21 or not t[0].startswith('g'):
            continue
        pic = int(t[2][1:])
        pics.setdefault(pic, []).append(t)
    if want == 'last':
        want = list(pics)[-2]   # the last complete picture
    want = int(want)
    entries, mem = collections.OrderedDict(), {}
    for t in pics[want]:
        src, mask = t[3], int(t[4][1:], 16)
        words = [int(x, 16) for x in t[5:21]]
        if 'data' in t:
            for chunk in t[t.index('data') + 1:]:
                parts = chunk[1:].split(':')
                base = int(parts[0], 16)
                for i, w in enumerate(parts[1:]):
                    mem[base + 4 * i] = int(w, 16)
        if mask & 1:
            entries[src] = words      # the last read by MPE0 = what it drew
    return want, list(entries.items()), mem


def ycc_to_rgb(img):
    y = np.clip(img[..., 0] * (255 / 219) / 255 - 16 / 219, 0, 1)
    cr = np.clip(img[..., 1] * (255 / 224) / 255 - 16 / 224, 0, 1) - 0.5
    cb = np.clip(img[..., 2] * (255 / 224) / 255 - 16 / 224, 0, 1) - 0.5
    rgb = np.stack([y + 1.402 * cr, y - 0.34413 * cb - 0.714136 * cr, y + 1.772 * cb], -1)
    return (np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)


def chans(p):
    """Y, Cr, Cb, Z (opacity)."""
    return np.array([(p >> 24) & 0xFF, (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF], np.float64)


class Canvas:
    def __init__(self, scale):
        self.s = scale
        self.img = np.zeros((240 * scale, 360 * scale, 3))
        self.img[...] = (0x10, 0x80, 0x80)
        ys, xs = np.mgrid[0:240 * scale, 0:360 * scale]
        # sample points in 360x240 units; the original samples at integer (corner) coordinates
        self.px = xs / scale
        self.py = ys / scale

    def line(self, x0, y0, x1, y1, ca, cb, width, peak):
        r = RADIUS_PER_WIDTH * width
        if r <= 0 or peak <= 0:
            return
        s = self.s
        xa, xb = int(max(0, (min(x0, x1) - r - 1) * s)), int(min(360 * s, (max(x0, x1) + r + 2) * s))
        ya, yb = int(max(0, (min(y0, y1) - r - 1) * s)), int(min(240 * s, (max(y0, y1) + r + 2) * s))
        if xa >= xb or ya >= yb:
            return
        px, py = self.px[ya:yb, xa:xb], self.py[ya:yb, xa:xb]
        dx, dy = x1 - x0, y1 - y0
        ll = dx * dx + dy * dy
        t = np.clip(((px - x0) * dx + (py - y0) * dy) / ll, 0, 1) if ll else np.zeros_like(px)
        d = np.hypot(px - (x0 + t * dx), py - (y0 + t * dy))
        # opacity: the colours' 4th byte (Z), interpolated like the colour (t3k_linefit: peak = 0.00888 x Z)
        z = ca[3] + (cb[3] - ca[3]) * t
        a = np.clip(peak * Z_PEAK * z * (1 - d / r), 0, 1)[..., None]
        col = ca[:3] + (cb[:3] - ca[:3]) * t[..., None]
        sub = self.img[ya:yb, xa:xb]
        sub += (col - sub) * a


def draw_entry(cv, e, mem, stats):
    e = list(e)
    ptr = e[7]
    cf0 = 0
    lfsr = 0x31415926
    prev = None
    pal = [0] * 16
    for _ in range(20000):
        w = mem.get(ptr)
        if w is None:
            stats['missing'] += 1
            return
        ptr += 4
        if w >> 16 == 0x8000:
            cmd = w & 0xF
            if cmd == 1:
                return
            if cmd == 2:
                prev = None
            elif cmd in (0, 5):
                v = mem.get(ptr, 0)
                ptr += 4
                e[5] = (e[5] & 0xFFFF0000) | (v & 0xFFFF) if cmd == 0 else (e[5] & 0xFFFF) | ((v & 0xFFFF) << 16)
            elif cmd == 3:
                e[2] = mem.get(ptr, 0)
                ptr += 4
            elif cmd == 4:
                e[3] = mem.get(ptr, 0)
                e[2] = mem.get(ptr + 4, 0)
                ptr += 8
            elif cmd == 6:
                base = mem.get(ptr, 0)
                ptr += 4
                pal = [mem.get(base + 4 * i, 0) for i in range(16)]
            elif cmd == 7:
                e[2] = pal[(w >> 8) & 0xF]
                e[3] = pal[(w >> 12) & 0xF]
            elif cmd == 8:
                cf0 += vm.sext((w & 0xFFFF) >> 4, 12)   # asl #16, asr #20
            elif cmd == 9:
                cf0 = vm.s32(mem.get(ptr, 0))
                ptr += 4
            else:
                stats['cmd%X' % cmd] += 1
            continue
        if prev is not None:
            lfsr = lfsr >> 1 if lfsr & 1 else (lfsr >> 1) ^ 0xA3000000
            # unsorted: the current vertex first; colour A belongs there, B at the previous vertex
            pts = [vm.s32(v) / 65536.0 for v in vm.segment(e, w, prev, lfsr, cf0, raw=True)]
            ca, cb = chans(e[2]), chans(e[3])
            width = e[5] & 0xFF
            # strength: w5 >> 2 relative to the fitted entries (all 4FFF0002 so far), less the jitter share (r18 = 1 - t)
            intensity = ((e[5] >> 2) & 0x3FFFFFFF) / float(0x13FFC000)
            t = min(max(vm.s32(e[12] + cf0), 0), 0xFFFF) / 65536.0
            cv.line(*pts, ca, cb, width, intensity * (1 - t))
            stats['segments'] += 1
        prev = w


def main():
    path, want, out = sys.argv[1:4]
    scale = int(sys.argv[4]) if len(sys.argv) > 4 else 2
    shot = sys.argv[5] if len(sys.argv) > 5 else None
    pic, entries, mem = parse(path, want)
    cv = Canvas(scale)
    stats = collections.Counter()
    for src, e in entries:
        if e[15] == 0x00710001:
            draw_entry(cv, e, mem, stats)
    print('picture %d: %d entries, %s' % (pic, len(entries), dict(stats)))
    from PIL import Image
    ours = Image.fromarray(ycc_to_rgb(cv.img))
    if shot:
        emu = Image.open(shot).convert('RGB').resize(ours.size, Image.NEAREST)
        both = Image.new('RGB', (ours.width * 2 + 8, ours.height))
        both.paste(emu, (0, 0))
        both.paste(ours, (ours.width + 8, 0))
        ours = both
    ours.save(out)


if __name__ == '__main__':
    main()

"""Tempest 3000 vector lines: measure the coverage profile of 806A3F50's line raster from a span log.

python -I tools/t3k_linefit.py <spans.txt> [out.png]

<spans.txt>: NUANCE_NOJIT=1 NUANCE_NOAOT=1 NUANCE_THREADS=0 NUANCE_PCLOG=2030072C NUANCE_SPANLOG=00710001 NUANCE_JOBLOG=<f>
Each 'pc' line (a segment: r8..r11 = endpoints 16.16, the entry with the current colours A / B in words 2 / 3) is
followed by MPE0's read / write span pairs for that segment. Per changed pixel: alpha = (written - read) /
(colour - read) on the channel with the largest difference, distance from the pixel to the segment, position t along it.
Prints alpha binned by distance per line width (w5 low byte); plots it if out.png is given.
"""
import collections
import math
import sys


def chans(p):
    return (p >> 24) & 0xFF, (p >> 16) & 0xFF, (p >> 8) & 0xFF


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def main():
    seg = None
    pend = {}
    samples = collections.defaultdict(list)   # width -> [(d, t, alpha, along_outside)]
    for line in open(sys.argv[1]):
        t = line.split()
        if not t:
            continue
        if t[0] == 'pc':
            if len(t) < 53 or t[52] != '00710001':
                seg = None
                continue
            r = [int(x, 16) for x in t[4:36]]
            e = [int(x, 16) for x in t[37:53]]
            x0, y0, x1, y1 = (s32(r[i]) / 65536.0 for i in (8, 9, 10, 11))
            seg = (x0, y0, x1, y1, e[2], e[3], e[5] & 0xFF, r[3])
            pend = {}
        elif t[0] == 'span' and seg:
            kind, x, y, n = t[1], int(t[2]), int(t[3]), int(t[4])
            px = [int(v, 16) for v in t[5:5 + n]]
            if kind == 'rd':
                pend[(x, y)] = px
                continue
            before = pend.pop((x, y), None)
            if before is None:
                continue
            x0, y0, x1, y1, ca, cb, width, swapped = seg
            dx, dy = x1 - x0, y1 - y0
            ll = dx * dx + dy * dy
            for i, (b, a) in enumerate(zip(before, px)):
                cx, cy = x + i, y
                tt = ((cx - x0) * dx + (cy - y0) * dy) / ll if ll else 0.0
                tc = min(max(tt, 0.0), 1.0)
                d = math.hypot(cx - (x0 + tc * dx), cy - (y0 + tc * dy))
                # colour along the line: try A -> B; the fit reports both directions' errors later
                col = [ca_c + (cb_c - ca_c) * tc for ca_c, cb_c in zip(chans(ca), chans(cb))]
                bc, ac = chans(b), chans(a)
                k = max(range(3), key=lambda j: abs(col[j] - bc[j]))
                den = col[k] - bc[k]
                if abs(den) < 24:
                    continue
                alpha = (ac[k] - bc[k]) / den
                samples[width].append((d, tt, alpha))
    for width in sorted(samples):
        s = samples[width]
        bins = collections.defaultdict(list)
        for d, tt, a in s:
            if 0.0 <= tt <= 1.0:
                bins[round(d * 4) / 4].append(a)
        print('width byte %d: %d samples' % (width, len(s)))
        print('   ' + ' '.join('%.2f:%.2f(%d)' % (d, sum(v) / len(v), len(v)) for d, v in sorted(bins.items()) if d <= 8))
    if len(sys.argv) > 2:
        from PIL import Image, ImageDraw
        img = Image.new('RGB', (800, 400), (0, 0, 0))
        dr = ImageDraw.Draw(img)
        colours = [(255, 80, 80), (80, 255, 80), (80, 160, 255), (255, 255, 80), (255, 80, 255), (80, 255, 255)]
        for wi, width in enumerate(sorted(samples)):
            for d, tt, a in samples[width][:20000]:
                if 0.0 <= tt <= 1.0:
                    dr.point((int(d * 80), int(380 - a * 300)), fill=colours[wi % len(colours)])
        img.save(sys.argv[2])


if __name__ == '__main__':
    main()

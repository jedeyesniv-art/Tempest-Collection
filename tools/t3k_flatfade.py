"""Tempest 3000 zoom feedback (00300004): the fade's exact rounding, measured where sampling can't matter.

python -I tools/t3k_flatfade.py <spanlog>   (tools/t3k_logrun.py --span 00300004 ...)

For each written pixel, the source position (PLAN.md mapping) and its 3x3 source neighbourhood; only pixels whose
whole neighbourhood is ONE colour are kept (bilinear / nearest / jitter all give that colour). Then per channel the
residual out - (n + (s - n) f) (n = 16 for Y, 128 for Cr / Cb) is tabulated: floor gives residuals in (-1, 0],
round in (-0.5, 0.5], and a bias shows as a shift. Also prints, per channel, the mean residual for sources above and
below neutral (an asymmetry there is what pushes the warp's chroma one way).
"""
import collections
import math
import sys


def chans(p):
    return (p >> 24) & 0xFF, (p >> 16) & 0xFF, (p >> 8) & 0xFF


def main():
    pics = collections.OrderedDict()
    cur = None
    for line in open(sys.argv[1]):
        t = line.split()
        if not t:
            continue
        if t[0] == 'spanentry':
            e = [int(x, 16) for x in t[1:17]]
            cur = pics.setdefault(tuple(e), (e, {}, []))
        elif t[0] == 'spanb' and cur and t[1] == 'rd':
            x, y, w, h = map(int, t[2:6])
            px = [int(v, 16) for v in t[6:6 + w * h]]
            for r in range(h):
                for i in range(w):
                    cur[1][(x + i, y + r)] = px[r * w + i]
        elif t[0] == 'span' and cur and t[1] == 'wr':
            x, y, n = map(int, t[2:5])
            for i, v in enumerate(t[5:5 + n]):
                cur[2].append((x + i, y, int(v, 16)))
    names = ('Y', 'Cr', 'Cb')
    neutral = (16, 128, 128)
    for e, src, wr in pics.values():
        if not src or not wr:
            continue
        cx, cy = e[0] >> 16, e[0] & 0xFFFF
        zx, zy = e[4] / 65536.0, e[5] / 65536.0
        a = -(e[6] & 0xFFFF) * 2 * math.pi / 65536.0
        f = (e[7] >> 16) / 16384.0
        res = [collections.Counter() for _ in range(3)]
        side = [[[], []] for _ in range(3)]   # residuals for source above / below neutral
        n = 0
        for (x, y, v) in wr:
            dx, dy = x - cx + 0.5, y - cy + 0.5
            sx = cx + (dx * math.cos(a) - dy * math.sin(a)) / zx
            sy = cy + (dx * math.sin(a) + dy * math.cos(a)) / zy
            ix, iy = int(math.floor(sx)), int(math.floor(sy))
            hood = [src.get((ix + i, iy + j)) for i in (-1, 0, 1) for j in (-1, 0, 1)]
            if None in hood or any(h != hood[0] for h in hood):
                continue
            s, o = chans(hood[0]), chans(v)
            n += 1
            for c in range(3):
                ideal = neutral[c] + (s[c] - neutral[c]) * f
                if not 0 <= ideal <= 255:
                    continue
                r = o[c] - ideal
                res[c][round(r * 4) / 4] += 1
                if s[c] != neutral[c]:
                    side[c][0 if s[c] > neutral[c] else 1].append(r)
        if n < 50:
            continue
        print(f'fade {f:.4f} zoom {zx:.4f} w9 {e[9]:08X}: {n} flat pixels')
        for c in range(3):
            tot = sum(res[c].values()) or 1
            hist = '  '.join(f'{k:+.2f}:{v / tot:4.0%}' for k, v in sorted(res[c].items()) if v / tot >= 0.01)
            up = side[c][0]
            dn = side[c][1]
            m = lambda l: (sum(l) / len(l)) if l else float('nan')
            print(f'  {names[c]:2s} residual {hist}   mean above n {m(up):+.2f} ({len(up)})  below n {m(dn):+.2f} ({len(dn)})')


if __name__ == '__main__':
    main()

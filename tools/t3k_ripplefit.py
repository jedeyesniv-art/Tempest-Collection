"""Tempest 3000 ripple feedback (entry type 0000000F, module 806B0B30; boot / title logo screens): output pixels vs the
model, from a span log (tools/t3k_logrun.py --span 0000000F): 'spanb rd' = the previous picture (w9's buffer) in
blocks, 'spanb wr' = the output.

python -I tools/t3k_ripplefit.py <spanlog>

Model (806B0D86..806B0EA4, read literally): T[i] = sin(2 pi (i + 1) / 64) px (16.16, the table at local $201004C0);
per output pixel (x, y): A = (w10 << 17) + x w4, B = w11 + y w5 + A; dx = T[(A >> 16) & 63], dy = T[(B >> 16) & 63];
s = bilinear(previous picture, x + dx + ox, y + dy + oy) per component (Y, Cr, Cb); out = s + (w2 - s) x w7 (2.30).
The offsets (ox, oy) are fitted here.
"""
import math
import sys

T = [int(65536 * math.sin(2 * math.pi * (i + 1) / 64)) for i in range(64)]


def chans(p):
    return [(p >> 24) & 0xFF, (p >> 16) & 0xFF, (p >> 8) & 0xFF]


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def main():
    pics = []
    cur = None
    for line in open(sys.argv[1]):
        t = line.split()
        if not t:
            continue
        if t[0] == 'spanentry':
            e = [int(x, 16) for x in t[1:17]]
            if cur is None or cur[0] != e:
                cur = (e, {}, {})
                pics.append(cur)
        elif t[0] == 'spanb' and cur:
            x, y, w, h = map(int, t[2:6])
            px = [int(v, 16) for v in t[6:6 + w * h]]
            dst = cur[1] if t[1] == 'rd' else cur[2]
            for r in range(h):
                for i in range(w):
                    dst[(x + i, y + r)] = px[r * w + i]
    print(len(pics), 'entries')
    for e, src, out in pics[:3]:
        w4, w5, w10, w11 = s32(e[4]), s32(e[5]), s32(e[10]), s32(e[11])
        f = s32(e[7]) / 2 ** 30
        col = chans(e[2])

        def model(x, y, ox, oy):
            A = (w10 << 17) + x * w4
            B = w11 + y * w5 + A
            sx = x + T[(A >> 16) & 63] / 65536 + ox
            sy = y + T[(B >> 16) & 63] / 65536 + oy
            x0, y0 = math.floor(sx), math.floor(sy)
            fx, fy = sx - x0, sy - y0
            p = [src.get((x0 + i, y0 + j)) for j in (0, 1) for i in (0, 1)]
            if any(q is None for q in p):
                return None
            c = [chans(q) for q in p]
            s = [(c[0][k] * (1 - fx) + c[1][k] * fx) * (1 - fy) + (c[2][k] * (1 - fx) + c[3][k] * fx) * fy for k in range(3)]
            return [s[k] + (col[k] - s[k]) * f for k in range(3)]

        pts = list(out.items())[::23]
        best = []
        for ox in (-2, -1.5, -1, -0.5, 0, 0.5, 1, 1.5, 2):
            for oy in (-2, -1.5, -1, -0.5, 0, 0.5, 1, 1.5, 2):
                err = n = 0
                for (x, y), v in pts:
                    m = model(x, y, ox, oy)
                    if m is None:
                        continue
                    o = chans(v)
                    err += sum(abs(o[k] - m[k]) for k in range(3))
                    n += 3
                if n:
                    best.append((err / n, ox, oy, n // 3))
        best.sort()
        print('w4 %08X w5 %08X w10 %08X w11 %08X w7 %08X: ' % (e[4], e[5], e[10], e[11], e[7]) +
              ' | '.join('err %.2f off (%.1f, %.1f) n %d' % b for b in best[:3]))


if __name__ == '__main__':
    main()

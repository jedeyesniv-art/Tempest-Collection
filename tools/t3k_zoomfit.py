"""Tempest 3000 zoom feedback (entry type 00300004, module 806A5280): fit how an output pixel samples the source.

python -I tools/t3k_zoomfit.py <spanlog>    (NUANCE_THREADS=0 NUANCE_SPANLOG=00300004 NUANCE_JOBLOG=<f>)

The log has 'spanentry' (the entry's words), 'spanb rd x y w h' source blocks (the previous picture) and 'span wr x y n'
output rows. Per entry: the source image is rebuilt from its blocks, then each mapping hypothesis
src = c + k * R(sign * angle) (p - c + o) is scored (k = 1 / zoom or zoom, angle from w6 at 16 bits a turn, o = a
pixel offset) by the Y channel error after the best fade factor; prints the best hypotheses and the fade.
"""
import collections
import math
import sys


def y_of(p):
    return (p >> 24) & 0xFF


def main():
    pics = []   # (entry words, source dict, writes list)
    cur = None
    for line in open(sys.argv[1]):
        t = line.split()
        if not t:
            continue
        if t[0] == 'spanentry':
            cur = ([int(x, 16) for x in t[1:17]], {}, [])
            pics.append(cur)
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
    print(len(pics), 'zoom entries')
    for e, src, wr in pics[::max(1, len(pics) // 8)]:
        if not src or not wr:
            continue
        cx, cy = e[0] >> 16, e[0] & 0xFFFF
        zx, zy = e[4] / 65536.0, e[5] / 65536.0
        ang = (e[6] & 0xFFFF) * 2 * math.pi / 65536.0
        best = []
        for kind in ('1/z', 'z'):
            for sign in (1, -1, 0):
                for ox in (0.0, 0.5, -0.5):
                    for oy in (0.0, 0.5, -0.5):
                        num = den = 0.0
                        pairs = []
                        for (x, y, v) in wr[::7]:
                            dx, dy = x - cx + ox, y - cy + oy
                            a = sign * ang
                            rx = dx * math.cos(a) - dy * math.sin(a)
                            ry = dx * math.sin(a) + dy * math.cos(a)
                            kx, ky = (1 / zx, 1 / zy) if kind == '1/z' else (zx, zy)
                            sx, sy = cx + rx * kx, cy + ry * ky
                            s = src.get((int(math.floor(sx)), int(math.floor(sy))))
                            if s is None:
                                continue
                            pairs.append((y_of(s) - 16, y_of(v) - 16))
                        if len(pairs) < 50:
                            continue
                        f = sum(a * b for a, b in pairs) / max(1, sum(a * a for a, b in pairs))
                        err = sum(abs(b - f * a) for a, b in pairs) / len(pairs)
                        best.append((err, kind, sign, ox, oy, f, len(pairs)))
        best.sort()
        print('w4 %08X w6 %08X w7 %08X: ' % (e[4], e[6], e[7]) +
              ' | '.join('err %.2f %s sign %d off (%.1f,%.1f) fade %.3f n %d' % b for b in best[:3]))


if __name__ == '__main__':
    main()

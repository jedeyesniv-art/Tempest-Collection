"""Tempest 3000 zoom feedback (00300004): fit the mapping per PICTURE (t3k_zoomfit.py took every band job's entry line
for a picture). Built for the spinning warp of level 3 ("Crosseyed and Painless", w6 = $FF70), 2026-11-17.

python -I tools/t3k_zoomfit2.py <run_span.txt>    (tools/t3k_logrun.py --span 00300004)

Per picture: the previous picture from its 'spanb rd' blocks, the module's output from 'span wr' rows; each output
pixel's Y predicted by a hypothesis (bilinear source sample, best fade about Y 16) and scored by mean |error|:
  pixel   s = c + R(sign a) (p - c) / z            (every pixel rotated: our renderer)
  tile    tile origin t mapped as 'pixel', inside the tile s = s(t) + (p - t) / z, no rotation
  none    no rotation
Output tiles = runs of 'span wr' between two source block reads (the module reads one 16x16 block per tile).
"""
import math
import re
import sys


def y_of(v):
    return (v >> 24) & 0xFF


def main():
    pics = {}
    order = []
    cur = None
    tile = -1
    for line in open(sys.argv[1]):
        t = line.split()
        if not t:
            continue
        if t[0] == 'spanentry':
            m = re.search(r' p(\d+) ', line)
            pid = int(m.group(1))
            if pid not in pics:
                pics[pid] = {'e': [int(x, 16) for x in t[1:17]], 'src': {}, 'wr': [], 'tiles': {}}
                order.append(pid)
            cur = pics[pid]
        elif t[0] == 'spanb' and cur is not None and t[1] == 'rd':
            x, y, w, h = map(int, t[2:6])
            px = [int(v, 16) for v in t[6:6 + w * h]]
            for r in range(h):
                for i in range(w):
                    cur['src'][(x + i, y + r)] = px[r * w + i]
        elif t[0] == 'span' and cur is not None and t[1] == 'wr':
            x, y, n = map(int, t[2:5])
            # tile = the 19-wide column block and the row band (tile origin = its first written pixel)
            key = x
            for i, v in enumerate(t[5:5 + n]):
                cur['wr'].append((x + i, y, int(v, 16), x))
    for pid in order[:4]:
        P = pics[pid]
        e = P['e']
        cx, cy = e[0] >> 16, e[0] & 0xFFFF
        # (w0 = 00B4 0078 = 180,120)
        zx, zy = e[4] / 65536.0, e[5] / 65536.0
        a0 = ((e[6] & 0xFFFF) - (65536 if e[6] & 0x8000 else 0)) * 2 * math.pi / 65536.0
        src = P['src']

        def sample(sx, sy):
            x0, y0 = math.floor(sx), math.floor(sy)
            fx, fy = sx - x0, sy - y0
            q = [src.get((x0, y0)), src.get((x0 + 1, y0)), src.get((x0, y0 + 1)), src.get((x0 + 1, y0 + 1))]
            if any(v is None for v in q):
                return None
            q = [y_of(v) for v in q]
            return (q[0] * (1 - fx) + q[1] * fx) * (1 - fy) + (q[2] * (1 - fx) + q[3] * fx) * fy

        def score(mapf):
            pairs = []
            for (x, y, v, tx) in P['wr'][::3]:
                s = mapf(x, y, tx)
                if s is None:
                    continue
                p = sample(*s)
                if p is None:
                    continue
                pairs.append((p - 16, y_of(v) - 16))
            if len(pairs) < 100:
                return None
            f = sum(a * b for a, b in pairs) / max(1e-9, sum(a * a for a, b in pairs))
            return sum(abs(b - f * a) for a, b in pairs) / len(pairs), f, len(pairs)

        res = []
        for ascale in (1.0, -1.0, 0.0, 2.0, 0.5):
            a = a0 * ascale
            ca, sa = math.cos(a), math.sin(a)
            for off in (0.0, 0.5):
                def pix(x, y, tx, ca=ca, sa=sa, off=off):
                    dx, dy = x + off - cx, y + off - cy
                    return cx + (dx * ca - dy * sa) / zx, cy + (dx * sa + dy * ca) / zy
                r = score(pix)
                if r:
                    res.append((r[0], 'pixel angle x%.1f off %.1f' % (ascale, off), r[1], r[2]))
                # tile: the tile's origin rotated, inside unrotated (tile origin: x = tile column start, same row)
                def tilef(x, y, tx, ca=ca, sa=sa, off=off):
                    dx, dy = tx + off - cx, y + off - cy
                    ox, oy = cx + (dx * ca - dy * sa) / zx, cy + (dx * sa + dy * ca) / zy
                    return ox + (x - tx) / zx, oy
                r = score(tilef)
                if r:
                    res.append((r[0], 'row-start angle x%.1f off %.1f' % (ascale, off), r[1], r[2]))
        res.sort()
        print('picture %d: centre %.1f,%.1f zoom %.4f %.4f angle %.3f deg, %d source px, %d written' % (
            pid, cx, cy, zx, zy, math.degrees(a0), len(src), len(P['wr'])))
        for r in res[:6]:
            print('   err %.3f  %-32s fade %.3f n %d' % r)
        # the fade per channel with the best map (every pixel rotated, as fitted): out = k (in - n) + n, unclipped
        # outputs only, n free; and the residual of the entry's own fade about the documented neutral
        a = -a0
        ca, sa = math.cos(a), math.sin(a)
        ent_f = (e[7] >> 16) / 16384.0
        for ch, sh, n0 in (('Y', 24, 16), ('Cr', 16, 128), ('Cb', 8, 128)):
            xs, ys = [], []
            for (x, y, v, tx) in P['wr'][::2]:
                dx, dy = x - cx, y - cy
                sx, sy = cx + (dx * ca - dy * sa) / zx, cy + (dx * sa + dy * ca) / zy
                x0, y0 = math.floor(sx), math.floor(sy)
                fx, fy = sx - x0, sy - y0
                q = [src.get((x0, y0)), src.get((x0 + 1, y0)), src.get((x0, y0 + 1)), src.get((x0 + 1, y0 + 1))]
                if any(w is None for w in q):
                    continue
                q = [(w >> sh) & 0xFF for w in q]
                s = (q[0] * (1 - fx) + q[1] * fx) * (1 - fy) + (q[2] * (1 - fx) + q[3] * fx) * fy
                o = (v >> sh) & 0xFF
                if 3 < o < 252:
                    xs.append(s)
                    ys.append(o)
            nn = len(xs)
            mx, my = sum(xs) / nn, sum(ys) / nn
            k = sum((p - mx) * (q - my) for p, q in zip(xs, ys)) / sum((p - mx) ** 2 for p in xs)
            b = my - k * mx
            n_fit = b / (1 - k) if abs(1 - k) > 1e-6 else float('nan')
            err_ent = sum(abs(q - (n0 + (p - n0) * ent_f)) for p, q in zip(xs, ys)) / nn
            bias_ent = sum(q - (n0 + (p - n0) * ent_f) for p, q in zip(xs, ys)) / nn
            err_fit = sum(abs(q - (k * p + b)) for p, q in zip(xs, ys)) / nn
            print('   %-2s fit out = %.4f in %+.2f (neutral %.1f): err %.3f | entry fade %.4f about %d: err %.3f, '
                  'bias %+.3f  (n %d, in %.0f..%.0f)' % (ch, k, b, n_fit, err_fit, ent_f, n0, err_ent, bias_ent,
                                                         nn, min(xs), max(xs)))


if __name__ == '__main__':
    main()

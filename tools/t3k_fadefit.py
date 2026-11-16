"""Tempest 3000 zoom feedback (00300004, module 806A5280): how the fade (w7 hi, 2.14) acts on Y, Cr and Cb.

python -I tools/t3k_fadefit.py <spanlog>   (tools/t3k_logrun.py --span 00300004 ...)

Each output pixel is paired with its source pixel (the mapping from PLAN.md: src = c + R(-a) (p - c + 1/2) / z, the
nearest pixel) and every channel is tested against:
  Y:     clip(Y * f)            clip(16 + (Y - 16) * f)
  Cr/Cb: unchanged              clip(128 + (C - 128) * f)  (signed chroma scaled)   clip(C * f)
with clipping to 0..255 (and, for comparison, the video range 16..235 / 16..240). Prints the mean absolute error and
the share of exact (+-1) matches per model, per picture.
"""
import math
import sys


def chans(p):
    return (p >> 24) & 0xFF, (p >> 16) & 0xFF, (p >> 8) & 0xFF


def clip(v, lo=0, hi=255):
    return max(lo, min(hi, v))


def main():
    pics = {}
    cur = None
    for line in open(sys.argv[1]):
        t = line.split()
        if not t:
            continue
        if t[0] == 'spanentry':
            e = [int(x, 16) for x in t[1:17]]
            key = (e[4], e[6], e[7])
            cur = pics.setdefault(key, (e, {}, []))
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
    models_y = {
        'Y*f': lambda s, f: clip(s * f),
        '16+(Y-16)f': lambda s, f: clip(16 + (s - 16) * f),
        'Y*f video': lambda s, f: clip(s * f, 16, 235),
    }
    models_c = {
        'same': lambda s, f: s,
        '128+(C-128)f': lambda s, f: clip(128 + (s - 128) * f),
        '128+(C-128)f video': lambda s, f: clip(128 + (s - 128) * f, 16, 240),
        'C*f': lambda s, f: clip(s * f),
    }
    for key, (e, src, wr) in list(pics.items())[:: max(1, len(pics) // 6)]:
        if not src or not wr:
            continue
        cx, cy = e[0] >> 16, e[0] & 0xFFFF
        zx, zy = e[4] / 65536.0, e[5] / 65536.0
        a = -(e[6] & 0xFFFF) * 2 * math.pi / 65536.0
        f = (e[7] >> 16) / 16384.0
        pairs = []
        for (x, y, v) in wr[::3]:
            dx, dy = x - cx + 0.5, y - cy + 0.5
            sx = cx + (dx * math.cos(a) - dy * math.sin(a)) / zx
            sy = cy + (dx * math.sin(a) + dy * math.cos(a)) / zy
            s = src.get((int(math.floor(sx)), int(math.floor(sy))))
            if s is not None:
                pairs.append((chans(s), chans(v)))
        print(f'w7 {e[7]:08X} fade {f:.4f} zoom {zx:.4f} angle w6 {e[6] & 0xFFFF:04X}: {len(pairs)} pixel pairs')
        for name, m in models_y.items():
            err = [abs(o[0] - m(s[0], f)) for s, o in pairs]
            print(f'   Y  {name:22s} mean {sum(err) / len(err):6.2f}  within 1: {sum(x <= 1 for x in err) / len(err):5.1%}')
        for ci, cn in ((1, 'Cr'), (2, 'Cb')):
            for name, m in models_c.items():
                err = [abs(o[ci] - m(s[ci], f)) for s, o in pairs]
                print(f'   {cn} {name:22s} mean {sum(err) / len(err):6.2f}  within 1: {sum(x <= 1 for x in err) / len(err):5.1%}')
        # a few raw examples with big source values
        ex = sorted(pairs, key=lambda p: -p[0][0])[:4]
        print('   examples (src Y Cr Cb -> out):', '  '.join(f'{s} -> {o}' for s, o in ex))


if __name__ == '__main__':
    main()

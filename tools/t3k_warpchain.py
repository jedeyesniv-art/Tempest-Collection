"""Tempest 3000 zoom feedback (00300004): chain-simulate the warp on the CPU with candidate samplers and compare the
colour drift with the emulated pictures (the orange-patch hunt, 2026-11-17).

python -I tools/t3k_warpchain.py <spanlog> [model ...]

The span log (tools/t3k_logrun.py --span 00300004) gives per picture the source rows the module read (= the previous
emulated picture) and the pixels it wrote. The simulation starts from the first picture's emulated source, applies
the model, and continues from its OWN output; what the other entries drew on top in the emulated run (particles,
lines) is carried over as the difference between the next source and this output. Per picture it prints the share of
pink (Cr > 150, Cb > 140) and orange (Cr > 150, Cb < 116) pixels, emulated vs simulated.

Models: bilin (bilinear at the mapped point - 1/2), bilin0 (bilinear at the mapped point), nearest (floor of the
mapped point), and each with _round instead of the module's floor in the fade.
"""
import math
import sys

import numpy as np

W, H = 360, 240


def parse(path):
    pics = []
    cur = None
    for line in open(path):
        t = line.split()
        if not t:
            continue
        if t[0] == 'spanentry':
            e = tuple(int(x, 16) for x in t[1:17])
            if cur is None or cur['e'] != e:
                cur = {'e': e, 'src': np.zeros((H, W, 3), np.float64), 'sm': np.zeros((H, W), bool),
                       'out': np.zeros((H, W, 3), np.float64), 'om': np.zeros((H, W), bool)}
                pics.append(cur)
        elif t[0] == 'spanb' and cur is not None and t[1] == 'rd':
            x, y, w, h = map(int, t[2:6])
            px = [int(v, 16) for v in t[6:6 + w * h]]
            for r in range(h):
                for i in range(w):
                    xx, yy = x + i, y + r
                    if 0 <= xx < W and 0 <= yy < H:
                        p = px[r * w + i]
                        cur['src'][yy, xx] = ((p >> 24) & 255, (p >> 16) & 255, (p >> 8) & 255)
                        cur['sm'][yy, xx] = True
        elif t[0] == 'span' and cur is not None and t[1] == 'wr':
            x, y, n = map(int, t[2:5])
            for i, v in enumerate(t[5:5 + n]):
                xx = x + i
                if 0 <= xx < W and 0 <= y < H:
                    p = int(v, 16)
                    cur['out'][y, xx] = ((p >> 24) & 255, (p >> 16) & 255, (p >> 8) & 255)
                    cur['om'][y, xx] = True
    return [p for p in pics if p['om'].sum() > W * H // 2]


def step(src, e, model):
    cx, cy = e[0] >> 16, e[0] & 0xFFFF
    zx, zy = e[4] / 65536.0, e[5] / 65536.0
    a = -(e[6] & 0xFFFF) * 2 * math.pi / 65536.0
    f = (e[7] >> 16) / 16384.0
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float64)
    dx, dy = xs - cx + 0.5, ys - cy + 0.5
    sx = cx + (dx * math.cos(a) - dy * math.sin(a)) / zx
    sy = cy + (dx * math.sin(a) + dy * math.cos(a)) / zy
    kind = model.split('_')[0]
    if kind == 'nearest':
        ix = np.clip(np.floor(sx).astype(int), 0, W - 1)
        iy = np.clip(np.floor(sy).astype(int), 0, H - 1)
        s = src[iy, ix]
    else:
        if kind == 'bilin':
            sx, sy = sx - 0.5, sy - 0.5
        x0 = np.floor(sx).astype(int); y0 = np.floor(sy).astype(int)
        fx = (sx - x0)[..., None]; fy = (sy - y0)[..., None]
        def at(yy, xx):
            return src[np.clip(yy, 0, H - 1), np.clip(xx, 0, W - 1)]
        top = at(y0, x0) + (at(y0, x0 + 1) - at(y0, x0)) * fx
        bot = at(y0 + 1, x0) + (at(y0 + 1, x0 + 1) - at(y0 + 1, x0)) * fx
        s = top + (bot - top) * fy
    n = np.array([16.0, 128.0, 128.0])
    v = n + (s - n) * f
    if model.endswith('_trunc'):   # toward neutral: small deviations can't run away either way
        v = n + np.trunc((s - n) * f)
    elif model.endswith('_round'):
        v = np.floor(v + 0.5)
    elif '_b' in model:   # floor with an offset: bilin_b0.375
        v = np.floor(v + float(model.split('_b')[1]))
    else:
        v = np.floor(v + 1e-9)
    return np.clip(v, 0, 255)


def err(a, b, mask):
    return np.abs(a - b)[mask].mean(axis=0)


def shares(img, mask):
    cr, cb = img[..., 1][mask], img[..., 2][mask]
    tot = max(1, mask.sum())
    return ((cr > 150) & (cb > 140)).sum() / tot, ((cr > 150) & (cb < 116)).sum() / tot


def main():
    pics = parse(sys.argv[1])
    models = sys.argv[2:] or ['bilin', 'bilin0', 'nearest', 'bilin_round']
    print(len(pics), 'pictures')
    sims = {m: pics[0]['src'].copy() for m in models}
    errs = {}
    print('pic  fade   emulated pink/orange  ' + '  '.join(f'{m:>14s}' for m in models))
    for k, p in enumerate(pics):
        e = p['e']
        row = []
        for m in models:
            z = step(sims[m], e, m)
            row.append(shares(z, p['om']))
            errs.setdefault(m, []).append(err(z, p['out'], p['om']))
            if k + 1 < len(pics):
                nxt = pics[k + 1]
                delta = np.where(p['om'][..., None], nxt['src'] - p['out'], 0.0)
                sims[m] = np.where(nxt['sm'][..., None], np.clip(z + delta, 0, 255), nxt['src'])
        emu = shares(p['out'], p['om'])
        print(f'{k:3d} {((e[7] >> 16) / 16384.0):.3f}   {emu[0]:5.1%} / {emu[1]:5.1%}       ' +
              '  '.join(f'{r[0]:5.1%}/{r[1]:5.1%}' for r in row))
    last = int(__import__('os').environ.get('CHAIN_LAST', len(pics)))
    print(f'mean abs error Y Cr Cb over pictures 0..{last - 1}:')
    for m in models:
        e = np.mean(errs[m][:last], axis=0)
        print(f'  {m:14s} {e[0]:6.2f} {e[1]:6.2f} {e[2]:6.2f}')


if __name__ == '__main__':
    main()

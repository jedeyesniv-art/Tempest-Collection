"""Tempest 3000 web surface (entry type 0011000B, module 806AD2B0): fit the textured lane quads against the emulator.

python -I tools/t3k_webfit.py <spanlog> <entrylog> [max pictures]

<spanlog>: NUANCE_THREADS=0 NUANCE_SPANLOG=0011000B NUANCE_JOBLOG=<f>  ('spanentry .. p<flip>', 'span rd/wr' pairs)
<entrylog>: NUANCE_ENTRYLOG=<f> NUANCE_ENTRYDATA=1 from the same run (the texture at w0 and the quad records MPE0
read, $800BACF0.. : 8 words a quad = 4 x (position: y hi / x lo signed half pixels about (180, 120); uv: u hi / v lo)).
Per written pixel: the quad holding it, (s, t) inside it (inverse bilinear; s along v0 -> v1, t along v0 -> v3), then
for each texel-indexing hypothesis the opacity implied by (out - dest) / (texel - dest); prints the hypotheses' fits.
"""
import collections
import math
import sys


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def chans(p):
    return ((p >> 24) & 255, (p >> 16) & 255, (p >> 8) & 255, p & 255)


def inv_bilinear(p, a, b, c, d):
    """(s, t) with p = lerp(lerp(a, b, s), lerp(d, c, s), t); None if outside."""
    # Newton iterations from the centre
    s = t = 0.5
    for _ in range(12):
        x = (1 - t) * ((1 - s) * a[0] + s * b[0]) + t * ((1 - s) * d[0] + s * c[0])
        y = (1 - t) * ((1 - s) * a[1] + s * b[1]) + t * ((1 - s) * d[1] + s * c[1])
        dxs = (1 - t) * (b[0] - a[0]) + t * (c[0] - d[0])
        dys = (1 - t) * (b[1] - a[1]) + t * (c[1] - d[1])
        dxt = ((1 - s) * d[0] + s * c[0]) - ((1 - s) * a[0] + s * b[0])
        dyt = ((1 - s) * d[1] + s * c[1]) - ((1 - s) * a[1] + s * b[1])
        det = dxs * dyt - dxt * dys
        if abs(det) < 1e-9:
            return None
        ex, ey = p[0] - x, p[1] - y
        s += (ex * dyt - ey * dxt) / det
        t += (dxs * ey - dys * ex) / det
    if -0.02 <= s <= 1.02 and -0.02 <= t <= 1.02:
        return s, t
    return None


def load_entries(path):
    pics = {}
    for line in open(path):
        tk = line.split()
        if len(tk) < 21 or not tk[0].startswith('g') or tk[20] != '0011000B' or 'data' not in tk:
            continue
        mask = int(tk[4][1:], 16)
        if not mask & 1:
            continue
        mem = {}
        for chunk in tk[tk.index('data') + 1:]:
            parts = chunk[1:].split(':')
            base = int(parts[0], 16)
            for i, w in enumerate(parts[1:]):
                mem[base + 4 * i] = int(w, 16)
        pics[int(tk[2][1:])] = (int(tk[5], 16), mem)
    return pics


def quads_of(mem):
    out = []
    a = 0x800BACF0
    while mem.get(a, 0) or mem.get(a + 4, 0):
        vs = []
        for k in range(4):
            pw, uvw = mem.get(a + 8 * k, 0), mem.get(a + 8 * k + 4, 0)
            # position word: y hi / x lo (806AD7BE adds 180 to the low half, 120 to the high half)
            vs.append(((180 + s16(pw) / 2.0, 120 + s16(pw >> 16) / 2.0), (uvw >> 16, uvw & 0xFFFF)))
        out.append(vs)
        a += 32
    return out


def tex_bilinear(tex, u, v):
    """806ADD66..: four texels around (u, v) (16 x 16, wrapping), weighted by the fractions; per channel."""
    iu, iv = math.floor(u), math.floor(v)
    fu, fv = u - iu, v - iv
    def t(a, b):
        return chans(tex[(b % 16) * 16 + (a % 16)])
    c00, c10, c01, c11 = t(iu, iv), t(iu + 1, iv), t(iu, iv + 1), t(iu + 1, iv + 1)
    return [(c00[k] * (1 - fu) + c10[k] * fu) * (1 - fv) + (c01[k] * (1 - fu) + c11[k] * fu) * fv for k in range(3)]


def edge_walk(p, q):
    """806AD2B0's span set-up: on scanline y the quad's left and right edge crossings, their (u, v) interpolated along
    the edges by y, then linear across by x. q = 4 x ((x, y), (u, v)). None if p is outside."""
    x, y = p
    xs = []
    for k in range(4):
        (x0, y0), (u0, v0) = q[k]
        (x1, y1), (u1, v1) = q[(k + 1) % 4]
        if y0 == y1 or not (min(y0, y1) <= y <= max(y0, y1)):
            continue
        f = (y - y0) / (y1 - y0)
        xs.append((x0 + (x1 - x0) * f, u0 + (u1 - u0) * f, v0 + (v1 - v0) * f))
    if len(xs) < 2:
        return None
    xs.sort()
    (xl, ul, vl), (xr, ur, vr) = xs[0], xs[-1]
    if not (xl <= x <= xr) or xr - xl < 1e-6:
        return None
    f = (x - xl) / (xr - xl)
    return ul + (ur - ul) * f, vl + (vr - vl) * f


def model_check(spans, ents, maxp, uscale, vscale, wdiv, off):
    """Mean abs Y error of: out = T + (dest - T) x w, T = bilinear texel at (u, v) interpolated over the quad from the
    vertex words (u = hi / uscale, v = lo / vscale), w = v / wdiv."""
    pics = load_entries(ents)
    pend = {}
    pic = None
    done = 0
    err = n = 0
    errs = [0.0, 0.0, 0.0]
    for line in open(spans):
        tk = line.split()
        if not tk:
            continue
        if tk[0] == 'spanentry':
            pic = pics.get(int(tk[17][1:]))
            job = int(tk[18][1:], 16) if len(tk) > 18 else 0   # the scroll: v + job / 2 texels when sampling
            if pic:
                done += 1
                if done > maxp:
                    break
                tex = [pic[1].get(pic[0] + 4 * i, 0) for i in range(256)]
                quads = quads_of(pic[1])
            continue
        if tk[0] != 'span' or not pic:
            continue
        x, y, cnt = int(tk[2]), int(tk[3]), int(tk[4])
        px = [int(v, 16) for v in tk[5:5 + cnt]]
        if tk[1] == 'rd':
            pend[(x, y)] = px
            continue
        before = pend.pop((x, y), None)
        if before is None:
            continue
        for i, (b, o) in enumerate(zip(before, px)):
            p = (x + i + off, y + off)
            for q in quads:
                uv = edge_walk(p, q)
                if not uv:
                    continue
                u, v = uv[0] / uscale, uv[1] / vscale
                T = tex_bilinear(tex, u, v + job / 2.0)
                w = min(max(v / wdiv, 0.0), 1.0)
                B, O = chans(b), chans(o)
                for ch in range(3):
                    errs[ch] += abs(T[ch] + (B[ch] - T[ch]) * w - O[ch])
                err += abs(T[0] + (B[0] - T[0]) * w - O[0])
                n += 1
                break
    print('   per channel Y / Cr / Cb: %.2f %.2f %.2f' % tuple(x / max(1, n) for x in errs))
    return err / max(1, n), n


def main():
    spans, ents = sys.argv[1], sys.argv[2]
    maxp = int(sys.argv[3]) if len(sys.argv) > 3 else 20
    if len(sys.argv) > 4 and sys.argv[4] == 'model':
        for us, vs, wd, off in ((512, 512, 32, 0.0), (512, 512, 32, 0.5)):
            e, n = model_check(spans, ents, maxp, us, vs, wd, off)
            print('u/%d v/%d w=v/%d offset %.1f: mean |Y error| %.2f over %d pixels' % (us, vs, wd, off, e, n))
        return
    pics = load_entries(ents)
    stats = collections.defaultdict(list)
    pend = {}
    pic = None
    done = 0
    for line in open(spans):
        tk = line.split()
        if not tk:
            continue
        if tk[0] == 'spanentry':
            flip = int(tk[17][1:])
            pic = pics.get(flip)
            if pic:
                done += 1
                if done > maxp:
                    break
                tex_base, mem = pic
                tex = [mem.get(tex_base + 4 * i, 0) for i in range(256)]
                quads = quads_of(mem)
            continue
        if tk[0] != 'span' or not pic:
            continue
        x, y, n = int(tk[2]), int(tk[3]), int(tk[4])
        px = [int(v, 16) for v in tk[5:5 + n]]
        if tk[1] == 'rd':
            pend[(x, y)] = px
            continue
        before = pend.pop((x, y), None)
        if before is None:
            continue
        for i, (b, o) in enumerate(zip(before, px)):
            for off in (0.0, 0.5):
                p = (x + i + off, y + off)
                for q in quads:
                    st = inv_bilinear(p, q[0][0], q[1][0], q[2][0], q[3][0])
                    if not st:
                        continue
                    s, t = st
                    for name, col, row in (('s,t', s, t), ('s,1-t', s, 1 - t), ('1-s,t', 1 - s, t),
                                           ('1-s,1-t', 1 - s, 1 - t)):
                        tx = tex[min(15, max(0, int(row * 16))) * 16 + min(15, max(0, int(col * 16)))]
                        tc, bc, oc = chans(tx), chans(b), chans(o)
                        k = max(range(3), key=lambda j: abs(tc[j] - bc[j]))
                        if abs(tc[k] - bc[k]) < 30:
                            continue
                        a = (oc[k] - bc[k]) / (tc[k] - bc[k])
                        stats[(off, name)].append((tc[3], a))
                    break
    for key, v in sorted(stats.items(), key=lambda kv: -len(kv[1])):
        byz = collections.defaultdict(list)
        for z, a in v:
            byz[z // 32].append(a)
        spread = statistics_spread(byz)
        print('offset %.1f texel (%s): n %d  alpha by texel Z: %s  | mean spread %.3f' % (
            key[0], key[1], len(v), ' '.join('%d:%.2f' % (zb * 32, sum(a) / len(a)) for zb, a in sorted(byz.items())),
            spread))


def statistics_spread(byz):
    tot = n = 0
    for a in byz.values():
        m = sum(a) / len(a)
        tot += sum(abs(x - m) for x in a)
        n += len(a)
    return tot / max(1, n)


if __name__ == '__main__':
    main()

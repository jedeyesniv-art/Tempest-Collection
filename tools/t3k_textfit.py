"""Tempest 3000 glowing text (entry type 0010000D, module 806AF1E8, mode 0 = a number): model vs the emulator.

python -I tools/t3k_textfit.py <spanlog> <entrylog> [max entries] [dump.png]

<spanlog>: NUANCE_THREADS=0 NUANCE_SPANLOG=0010000D NUANCE_JOBLOG=<f>; <entrylog>: NUANCE_ENTRYLOG + NUANCE_ENTRYDATA=1,
same run (logs/t3k/gt/span.sh).

Module (read from the disassembly, lib addresses):
  set-up 806AF332: number = word at $800DC240, decimal digits (most significant first); glyph k = 1 KB (16 x 16 YCrCbZ)
    at w10 + digit * 1024; tex2 = 256 words (16 x 16) at w11; mapping block = 20 words at w12 -> local $DF0:
    DF0 (glyph u0, glyph v0, tex2 u0, tex2 v0), E10 (tex2 du, dv per pixel, d2u, d2v), E20 (glyph row step: E24
    replaced), E30 (tex2 row step du, dv, d2u, d2v). E00 = glyph du per pixel = 2^40 / w2, E24 = 2^40 / w3.
    Cell = 16 x w2 by 16 x w3 at w0 (x hi, y lo); advance 12 x w2 (digit 1: 4 x w2); w14 == 1 centres.
  per digit 806AF512: tex2 origin stepped DB0 pixels along x (DB0 = the advance of the digits before; the stepped
    origin and du / dv are written back, so later digits step again: kept as is).
  per pixel 806AF7AE: G = bilinear glyph at (u, v) (16.16 texels); T = bilinear tex2 at (U + w4 x G.Cr >> 20,
    V + w5 x G.Cb >> 20); out = dest + (T - dest) x (G.Z << 21) x w6 >> 16 >> 30. Steps: u += du >> 8, du += d2u...
"""
import math
import sys
import os
ZSH = int(os.environ.get("ZSH", "24"))
MINFLIP = int(os.environ.get("MINFLIP", "700"))


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def chans(p):
    return ((p >> 24) & 255, (p >> 16) & 255, (p >> 8) & 255, p & 255)


def load_entries(path):
    pics = {}
    for line in open(path):
        tk = line.split()
        if len(tk) < 21 or not tk[0].startswith('g') or tk[20] != '0010000D' or 'data' not in tk:
            continue
        if not int(tk[4][1:], 16) & 1:
            continue
        mem = {}
        for chunk in tk[tk.index('data') + 1:]:
            parts = chunk[1:].split(':')
            base = int(parts[0], 16)
            for i, w in enumerate(parts[1:]):
                mem[base + 4 * i] = int(w, 16)
        words = [int(t, 16) for t in tk[5:21]]
        pics.setdefault(int(tk[2][1:]), []).append((words, mem))
    return pics


def bilinear(tex, u, v):
    """u, v 16.16; 16 x 16 wrapping; components as bytes (float)."""
    iu, iv = u >> 16, v >> 16
    fu, fv = (u & 0xFFFF) / 65536.0, (v & 0xFFFF) / 65536.0
    def t(a, b):
        return chans(tex[(b & 15) * 16 + (a & 15)])
    c00, c10, c01, c11 = t(iu, iv), t(iu + 1, iv), t(iu, iv + 1), t(iu + 1, iv + 1)
    return [(c00[k] + (c10[k] - c00[k]) * fu) + ((c01[k] + (c11[k] - c01[k]) * fu) - (c00[k] + (c10[k] - c00[k]) * fu)) * fv
            for k in range(4)]


class Text:
    def __init__(self, words, mem):
        self.w = words
        self.mem = mem
        num = mem.get(0x800DC240, 0)
        self.digits = [int(c) for c in str(num)]
        w2, w3 = words[2], words[3]
        self.adv = [((0x40000 if d == 1 else 0xC0000) * w2 >> 16) >> 16 for d in self.digits]
        self.cellw, self.cellh = (16 * w2) >> 16, (16 * w3) >> 16
        self.du, self.dv = (1 << 40) // w2 - 1, (1 << 40) // w3 - 1   # the module's reciprocal is a hair low
        blk = [mem.get(words[12] + 4 * i, 0) for i in range(20)]
        self.blk = [s32(b) for b in blk]
        self.tex2 = [mem.get(words[11] + 4 * i, 0) for i in range(256)]
        self.x0 = s32(words[0]) >> 16
        self.y0 = s32(words[0] << 16) >> 16
        if words[14] == 1:   # 806AF45A: centred on w0 (13 units a digit, 5 for a 1; half a cell high)
            units = sum(5 if d == 1 else 13 for d in self.digits)
            self.x0 -= (w2 * units) >> 17
            self.y0 -= w3 >> 13
        # per digit: x, glyph, tex2 state (DF0 u/v, E10 du/dv) after its DB0 step
        df0u, df0v, e10 = self.blk[2], self.blk[3], self.blk[8:12]
        e10 = list(e10)
        self.cells = []
        x, db0 = self.x0, 0
        for d, a in zip(self.digits, self.adv):
            for _ in range(db0):
                df0u += e10[0] >> 8
                df0v += e10[1] >> 8
                e10[0] += e10[2]
                e10[1] += e10[3]
            glyph = [mem.get(words[10] + d * 1024 + 4 * i, 0) for i in range(256)]
            self.cells.append((x, glyph, df0u, df0v, list(e10)))
            x += a
            db0 += a

    def pixel(self, cell, i, j, dest, w4=None, band_top=False):
        # 806AF5A0 skips (rows above the band - 1): a band starting inside the cell repeats the previous row
        if not band_top and j > 0:
            j -= 1
        x, glyph, U0, V0, e10 = cell
        blk = self.blk
        # rows: glyph v += dv (E24 >> 8, no second order in play); tex2 row origin stepped by E30
        u, v = blk[0], blk[1]
        U, V = U0, V0
        r0, r1 = blk[16], blk[17]
        for _ in range(j):
            U += r0 >> 8
            V += r1 >> 8
            r0 += blk[18]
            r1 += blk[19]
            v += self.dv >> 8
        du = self.du
        a, b = e10[0], e10[1]
        for _ in range(i):
            u += du >> 8
            U += a >> 8
            V += b >> 8
            a += e10[2]
            b += e10[3]
        g = bilinear(glyph, u, v)
        w4 = s32(self.w[4]) if w4 is None else w4
        cr, cb = int((g[1] - 128) * (1 << 22)), int((g[2] - 128) * (1 << 22))
        tu = U + ((w4 * cr) >> 20)
        tv = V + ((s32(self.w[5]) * cb) >> 20)
        t = bilinear(self.tex2, tu, tv)
        f = (int(g[3] * (1 << ZSH)) >> 1) * (self.w[6] & 0xFFFFFFFF) >> 16
        f /= float(1 << 30)
        return [dest[k] + (t[k] - dest[k]) * f for k in range(3)], f, g, t


def main():
    spans, ents = sys.argv[1], sys.argv[2]
    maxe = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    pics = load_entries(ents)
    pend = {}
    text = cell = None
    n = 0
    errs = [0.0] * 3
    big = 0
    entries = 0
    worst = []
    for line in open(spans):
        tk = line.split()
        if not tk:
            continue
        if tk[0] == 'spanentry':
            flip = int(tk[17][1:])
            words = [int(t, 16) for t in tk[1:17]]
            text = cell = None
            for ew, mem in (pics.get(flip, []) if flip >= MINFLIP else []):
                if ew[1:] == words[1:] or (ew[2:] == words[2:]):
                    t = Text(ew, mem)
                    x = s32(words[0]) >> 16
                    for c in t.cells:
                        if c[0] == x:
                            text, cell = t, c
                    break
            if text:
                entries += 1
                if entries > maxe:
                    break
            continue
        if tk[0] != 'span' or not text:
            continue
        x, y, cnt = int(tk[2]), int(tk[3]), int(tk[4])
        px = [int(v, 16) for v in tk[5:5 + cnt]]
        if tk[1] == 'rd':
            pend[(x, y)] = px
            continue
        before = pend.pop((x, y), None)
        if before is None:
            continue
        for k, (bp, op) in enumerate(zip(before, px)):
            i, j = x + k - cell[0], y - text.y0
            if not (0 <= i < text.cellw and 0 <= j < text.cellh):
                continue
            B, O = chans(bp), chans(op)
            out, f, g, t = text.pixel(cell, i, j, B)
            e = [abs(out[c] - O[c]) for c in range(3)]
            for c in range(3):
                errs[c] += e[c]
            if max(e) > 3:
                big += 1
                if len(worst) < 12:
                    worst.append((i, j, B, O, [round(v, 1) for v in out], round(f, 3), [round(v) for v in g],
                                  [round(v) for v in t]))
            n += 1
    print('%d entries, %d pixels; mean |error| Y / Cr / Cb %.2f %.2f %.2f; %d pixels off by > 3' % (
        entries, n, errs[0] / max(1, n), errs[1] / max(1, n), errs[2] / max(1, n), big))
    for wv in worst:
        print('  i %d j %d dest %s out %s model %s f %s glyph %s tex2 %s' % wv)


if __name__ == '__main__':
    main()

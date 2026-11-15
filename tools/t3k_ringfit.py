"""Tempest 3000 ring (entry type 00500005, module 806A7DC0): model vs the emulator's written pixels.

python -I tools/t3k_ringfit.py <spanlog> <entrylog>
Logs: python -I tools/t3k_logrun.py --state play_f2880 --fields 30 --span 00500005 --entries --out <prefix>

Model (806A7EDA.., registers logged with NUANCE_PCLOG at local 20300760 / 20300790):
  centre (cx, cy) = w0 (x hi, y lo); n = w1 >> 16; rx = ((w4 hi as asr 8 of w4) x n) >> 16, ry = ((w4 lo << 8) x n)
  >> 16 (whole pixels); d = col^2 x (2^28 / rx^2) >> 12 + row^2 x (2^28 / ry^2) >> 12 (16.16: col^2 / rx^2 + ...);
  t = w1 & $FFFF; skipped where d <= 1 - 2t; e = t - |d - (1 - t)|, skipped where e <= 0;
  k = e x (r8 - 1) / 2^30 with r8 = (w5 >> 2) / t (the peak, k at the ring's middle = (w5 >> 2) / 2^30);
  T = bilinear lookup of the 16 x 8 texture at w12 (128 words, wrapping) at u = |w13 hi x col + w2|,
  v = |w13 lo x row + w3| (16.16 texels); out = dest + (T - dest) x k. Each row is computed for the left half
  (col = x - cx < 0) and mirrored (col = cx - 1 - x) on the right, strength included. w5 is unsigned.
  Fit (play_f2880, 283 entries, 9074 pixels): 0.48 / 0.54 / 0.62, none off by > 3.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import t3k_spanlib as sl  # noqa: E402

CONST_RIGHT = os.environ.get('RING_CONST_RIGHT', '0') == '1'   # test: the rejected reading
OX = int(os.environ.get('RING_OX', '0'))
OY = int(os.environ.get('RING_OY', '0'))


class Ring:
    def __init__(self, w, mem):
        self.w = w
        self.tex = sl.read_block(mem, w[12], 128)
        self.cx, self.cy = sl.hi16(w[0]), sl.lo16(w[0])
        n = w[1] >> 16
        self.rx = ((sl.s32(w[4]) >> 8) * n) >> 16
        self.ry = (((w[4] & 0xFFFF) << 8) * n) >> 16
        self.t = w[1] & 0xFFFF
        self.ok = self.rx > 0 and self.ry > 0 and self.t > 0
        if self.ok:
            self.kx = (1 << 28) // (self.rx * self.rx)
            self.ky = (1 << 28) // (self.ry * self.ry)
            self.r7 = (w[5] >> 2) // self.t - 1   # (w5 unsigned)

    def texel(self, u, v):
        iu, iv = math.floor(u), math.floor(v)
        fu, fv = u - iu, v - iv

        def t(a, b):
            return sl.chans(self.tex[(b & 7) * 16 + (a & 15)])
        c00, c10, c01, c11 = t(iu, iv), t(iu + 1, iv), t(iu, iv + 1), t(iu + 1, iv + 1)
        return [(c00[k] + (c10[k] - c00[k]) * fu) * (1 - fv) + (c01[k] + (c11[k] - c01[k]) * fu) * fv for k in range(3)]

    def strength(self, col, row):
        d = ((col * col * self.kx) >> 12) + ((row * row * self.ky) >> 12)
        if d <= 0x10000 - 2 * self.t:
            return None
        e = self.t - abs(d - (0x10000 - self.t))
        if e <= 0:
            return None
        return e * self.r7 / 2 ** 30

    def pixel(self, x, y, dest):
        # each row: the left half computed (col = x - cx < 0), the right half mirrored from it (col = cx - 1 - x)
        if not self.ok:
            return None
        row = y - self.cy
        right = x >= self.cx
        col = (self.cx - 1 - x) if right else (x - self.cx)
        k = self.strength(col, row)
        if right and CONST_RIGHT:   # rejected: the mirrored pass reusing the row's last strength (4.5 vs 0.5)
            k = self.strength(-1, row)
            if self.strength(col, row) is None:
                return list(dest[:3])
        if k is None:
            return list(dest[:3])
        w = self.w
        tc = col
        u = abs(sl.s32((sl.s32(w[13]) >> 16) * tc + sl.s32(w[2]))) / 65536
        v = abs(sl.s32(sl.lo16(w[13]) * row + sl.s32(w[3]))) / 65536
        T = self.texel(u, v)
        return [dest[c] + (T[c] - dest[c]) * k for c in range(3)]


def main():
    spans, entlog = sys.argv[1], sys.argv[2]
    ents = sl.load_entries(entlog, 0x00500005)

    def prepare(ev):
        hit = sl.entry_for(ents, ev, match_from=0)
        return Ring(*hit) if hit else None

    def predict(ring, x, y, dest):
        return ring.pixel(x, y, dest)

    sl.fit(spans, prepare, predict)


if __name__ == '__main__':
    main()

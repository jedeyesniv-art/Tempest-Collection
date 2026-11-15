"""Tempest 3000 textured plane (entry type 00100200, module 806A3108 mode 2, handler 806A3794): model vs the emulator.

python -I tools/t3k_planefit.py <spanlog> <entrylog>
Logs: python -I tools/t3k_logrun.py --state play_f4600 --fields 30 --span 00100200 --entries --out <prefix>

Model: the rectangle w0 (x hi, y lo) / w1 (w hi, h lo) filled (no blend) with a bilinear lookup of the 16 x 16 texture at
w9 (wrapping) at (u, v), 16.16 texels: for row j, column i (from the rectangle's corner)
  u = w2 + sum_{r<j} (w7 + r x w11 hi) + the column sum of steps (w4 + j x w12 hi) growing by w6 hi a pixel, taken in
  64-pixel chunks that each start one step further on (the loop's pipelining); v likewise with w3, w10, w11 lo,
  w5, w12 lo, w6 lo (the hi / lo halves signed).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import t3k_spanlib as sl  # noqa: E402

PIX = os.environ.get('PLANE_PIX', 'corner')


class Plane:
    def __init__(self, w, mem):
        self.w = w
        self.tex = sl.read_block(mem, w[9], 256)
        self.x0, self.y0 = sl.hi16(w[0]), sl.lo16(w[0])

    def uv(self, i, j):
        # rows: plain sums; columns: in 64-pixel chunks from the rectangle's left edge, each chunk starting one
        # step further on (the pipelined loop 806A3904 adds the step once more at a chunk's end; logged chunk starts
        # match exactly)
        w = self.w
        s = sl.s32
        h = lambda v: sl.s16(v >> 16)
        l = lambda v: sl.s16(v)
        u = s(w[2]) + j * s(w[7]) + h(w[11]) * j * (j - 1) // 2
        v = s(w[3]) + j * s(w[10]) + l(w[11]) * j * (j - 1) // 2
        du = s(w[4]) + j * h(w[12])
        dv = s(w[5]) + j * l(w[12])
        a, b = h(w[6]), l(w[6])
        k, p = divmod(i, 64)
        for _ in range(k):   # whole chunks: 65 additions of the step, 64 increments of it
            u += 65 * du + a * 64 * 65 // 2
            v += 65 * dv + b * 64 * 65 // 2
            du += 64 * a
            dv += 64 * b
        return u + p * du + a * p * (p - 1) // 2, v + p * dv + b * p * (p - 1) // 2

    def pixel(self, x, y):
        u, v = self.uv(x - self.x0, y - self.y0)
        return sl.bilinear16(self.tex, u & 0xFFFFFFFF if u < 0 else u, v)[:3]


def main():
    spans, entlog = sys.argv[1], sys.argv[2]
    ents = sl.load_entries(entlog, 0x00100200)

    def prepare(ev):
        hit = sl.entry_for(ents, ev, match_from=0)
        return Plane(*hit) if hit else None

    def predict(p, x, y, dest):
        u, v = p.uv(x - p.x0, y - p.y0)
        return sl.bilinear16(p.tex, u / 65536.0, v / 65536.0)[:3]

    sl.fit(spans, prepare, predict)


if __name__ == '__main__':
    main()

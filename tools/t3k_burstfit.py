"""Tempest 3000 powerup burst (entry type 00310208, module 806A5FB0 mode 2, handler 806A6182): model vs the emulator's
disc calls.

python -I tools/t3k_burstfit.py <pclog span file>

Log: python -I tools/t3k_logrun.py --state <state> --at F --fields 12 --out <prefix> --pc 203005E6
     --mem 20100B98,20100B9C,20100BA0,20100B40 --interp 806A5FB0-806A6E40 --pictures P0:P1
(203005E6 = the jsr to the disc routine: r14 / r15 top-left 16.16, r6 size << 16, r13 intensity 2.30.)

Model: camera 3 words at $800DAD80 (y, x, z); colours 8 words at $8010B150 (one a ring). z0 = w2 + cam z (<= 0: none).
Five rings (I 1/64, 4/64, 7/64, 10/64, 63/64; size 16, 13, 10, 7, 4; radius $4000 .. $10000 step $3000) of 16
discs: A = w4 + w6 (+$4000 a disc), B = w5 (+$800 a disc); a = cos A >> 14; offsets dx = cos B a >> 30 x radius >> 16,
dy = (sin A >> 14) x radius >> 16, dz = (sin B a >> 30) x radius >> 16; x = w0 + dx << 5, y = w1 + dy << 5,
depth = z0 + dz << 6 + $800000 (<= 0: skipped); k = 0.75 x 2^44 / depth >> 16; X = ((x asr 1) + cam x 8) k >> 19
+ 180.5, Y likewise, size = n k >> 19 clamped 2..24.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import t3k_dotfit as df  # noqa: E402


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def mul(a, b, sh):
    return s32((a * b) >> sh)


def burst(e, camy, camx, camz):
    out = []
    z0 = s32(e[2] + camz)
    if z0 <= 0:
        return out
    I, size_n, rad = 0x01000000, 16, 0x4000
    while True:
        A, B = s32(e[4] + e[6]), s32(e[5])
        for _ in range(16):
            cA, sA = df.sincos(A)
            cB, sB = df.sincos(B)
            a = cA >> 14
            dz = mul(a, sB, 30)
            dx = mul(cB, a, 30)
            dx, dy, dz = mul(rad, dx, 16), mul(rad, sA >> 14, 16), mul(rad, dz, 16)
            x, y = s32(s32(e[0]) + (dx << 5)), s32(s32(e[1]) + (dy << 5))
            d = s32(z0 + (dz << 6)) + 0x800000
            if d > 0:
                k = df.recip_k(d - 0x800000)
                X = mul(k, (x >> 1) + camx * 8, 19) + 0xB48000
                Y = mul(k, (y >> 1) + camy * 8, 19) + 0x788000
                sz = min(max(mul(k, size_n, 19), 2), 24)
                out.append((X - (sz << 15), Y - (sz << 15), sz, I))
            A, B = s32(A + 0x4000), s32(B + 0x800)
        I += 0x03000000
        size_n -= 3
        rad += 0x3000
        if size_n == 4:
            I = 0x3F000000
        if size_n <= 1:
            return out


def main():
    calls = {}
    for line in open(sys.argv[1]):
        if not line.startswith('pc ') or ' 203005E6 ' not in line:
            continue
        tk = line.split()
        pic = tk[2]
        r = [int(v, 16) for v in tk[5:37]]
        ei = tk.index('e')
        e = [int(v, 16) for v in tk[ei + 1:ei + 17]]
        mi = tk.index('m20100B98')
        cam = [int(v, 16) for v in tk[mi + 1:mi + 4]]
        calls.setdefault(pic, (e, cam, []))[2].append((r[14], r[15], r[6] >> 16, r[13]))
    for pic, (e, cam, got) in calls.items():
        want = burst(e, s32(cam[0]), s32(cam[1]), s32(cam[2]))
        worst = 0.0
        n = min(len(got), len(want))
        for g, w in zip(got, want):
            worst = max(worst, abs(s32(g[0]) - w[0]) / 65536, abs(s32(g[1]) - w[1]) / 65536)
            if g[2] != w[2] or g[3] != w[3]:
                print('  %s size / I differ: got %s want %s' % (pic, g, w))
        print('%s: calls %d, model %d, worst position error %.4f px' % (pic, len(got), len(want), worst))


if __name__ == '__main__':
    main()

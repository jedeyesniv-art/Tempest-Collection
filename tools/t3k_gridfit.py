"""Tempest 3000 disc grid (entry type 00310010, module 806A6E40 mode 0, handler 806A7012): the disc list vs the
emulator's calls of the module's disc routine (local 20300A58).

python -I tools/t3k_gridfit.py <pclog span file> <entrylog>
Logs: python -I tools/t3k_logrun.py --state play_f3150 --fields 12 --pc 20300A58 --interp 806A6E00-806A7D00 --entries
      --out <prefix>

Model (806A7012..806A7414, literal): a 32 x 32 bitmask from w11 (32 words); rows walked from the middle out (words 16..31
forward, then 15..0 backward, the row step negated), each row word likewise from its middle (bits 15..0 forward, then
16..31 backward); positions accumulate steps that grow by w2 / w4 per point and w3 / w5 per row (quadratic). Each set
bit draws the 0031020E template (tools/t3k_flowerfit.py) at the point: last rotation angle (x + y) >> 12, depth =
the point's z (+ the tilt), size capped at 30 px, centred at (192, 120).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import t3k_spanlib as sl  # noqa: E402
from t3k_flowerfit import mul, sincos, recip_bits  # noqa: E402


def template(w, mem, px, py, pz, out):
    hdr = sl.read_block(mem, w[10], 4)
    pal = sl.read_block(mem, hdr[0], 16)
    r16, r17 = hdr[1], hdr[2]
    recs = sl.read_block(mem, w[10] + 16, 64)
    m0, n0 = r17 >> 16, r17 & 0xFFFF
    step_b = (1 << 16) // m0 if m0 else 0
    step_a = (1 << 16) // n0 if n0 else 0
    w1 = sl.s32(w[1])
    c3, s3 = sincos(((px + py) >> 12) & 0xFFFF)
    for rec in recs:
        size_bits = (rec >> 8) & 0x7F
        if size_bits == 0:
            break
        r29 = size_bits << 16
        a = mul(((rec >> 24) & 0xFF) << 8, r16, 16) + (w1 << 8)
        r28 = mul(mul(sl.s32(((rec >> 16) & 0xFF) << 24) >> 24 << 10, w1, 16), sl.s32(w[12]), 15)
        inten = mul(((rec >> 4) & 15) << 13, (0x100 - w1) << 21, 16)
        b, n, m = 0, n0, m0
        while True:
            c, s = sincos(a & 0xFFFF)
            x = mul(r28, c, 14) + mul(r28, -s, 14)
            y = mul(r28, s, 14) + mul(r28, c, 14)
            c2, s2 = sincos((b + (w1 << 6)) & 0xFFFF)
            depth = mul(x, s2, 30)
            x = mul(c2, x, 30)
            x, y = mul(x, c3, 30) + mul(y, -s3, 30), mul(x, s3, 30) + mul(y, c3, 30)
            x += px
            y += py
            d = max(pz + (depth << 2), 0xA0000)
            k = recip_bits(d, 16)
            x, y = mul(k, x, 19), mul(k, y, 19)
            size = min(mul(r29, k, 19) + 0x10000, 0x1E0000)
            x += 0xC00000
            y += 0x780000
            out.append((x - (size >> 1), y - (size >> 1), size, inten, pal[rec & 15]))
            a += step_a
            n -= 1
            if n > 0:
                continue
            b += step_b
            m -= 1
            if m > 0:
                continue
            break


def discs(w, mem):
    out = []
    rows = sl.read_block(mem, w[11], 32)
    w2, w3, w4, w5 = (sl.s32(v) for v in w[2:6])
    d0, d1, d2 = w2 >> 18, sl.lo16(w2), w4 << 3      # per-point step growth
    e0, e1, e2 = w3 >> 18, sl.lo16(w3), w5 << 3      # per-row step growth
    w13 = w[13]
    st = [(sl.s32(w13) >> 16) << 8, 0, 0]   # point step (x, y, z) at the start of a row: w13 hi

    def row(word, p):
        r12, r13, r14 = st
        x, y, z = p
        r15 = ((word >> 15) | (word << 17)) & 0xFFFFFFFF   # rot right 15
        start = (x, y, z, r12, r13, r14, r15)
        for _ in range(16):
            if r15 & 1:
                template(w, mem, x, y, z, out)
            r15 = ((r15 << 1) | (r15 >> 31)) & 0xFFFFFFFF
            x += r12
            r12 += d0
            y += r13
            r13 += d1
            z += r14
            r14 += d2
        x, y, z, r12, r13, r14, r15 = start
        r12, r13, r14 = -r12, -r13, -r14
        for _ in range(16):
            r14 -= d2
            r12 -= d0
            r13 -= d1
            x += r12
            y += r13
            z += r14
            r15 = ((r15 >> 1) | (r15 << 31)) & 0xFFFFFFFF
            if r15 & 1:
                template(w, mem, x, y, z, out)

    # rows: forward from word 16, then backward from word 15
    p = [0, 0, sl.s32(w[9])]
    r20, r21, r22 = 0, sl.lo16(w13) << 8, 0   # row step: w13 lo in y
    start = (list(p), r20, r21, r22)
    for i in range(16):
        row(rows[16 + i], p)
        p[2] += r22
        r22 += e2
        p[0] += r20
        r20 += e0
        p[1] += r21
        r21 += e1
    p, r20, r21, r22 = start
    p = list(p)
    r20, r21, r22 = -r20, -r21, -r22
    for i in range(16):
        r20 -= e0
        r21 -= e1
        r22 -= e2
        p[0] += r20
        p[1] += r21
        p[2] += r22
        row(rows[15 - i], p)
    return out


def main():
    pcl, entlog = sys.argv[1], sys.argv[2]
    ents = sl.load_entries(entlog, 0x00310010)
    calls = {}
    for line in open(pcl):
        if line.startswith('pc '):
            ev = sl.parse_pc(line.split())
            calls.setdefault(ev.picture, []).append(ev)
    for pic, cs in sorted(calls.items()):
        if pic not in ents:
            continue
        w, mem = ents[pic][0]
        model = discs(w, mem)
        print('picture %d: %d calls, model %d discs' % (pic, len(cs), len(model)))
        bad = 0
        for i, (ev, mo) in enumerate(zip(cs, model)):
            r = ev.r
            got = (sl.s32(r[4]), sl.s32(r[5]), r[6], r[13])
            if any(abs(p - q) > 64 for p, q in zip(got, mo[:4])):
                bad += 1
                if bad <= 4:
                    print('  disc %d: log x %.3f y %.3f size %.3f I %08X | model x %.3f y %.3f size %.3f I %08X' % (
                        i, got[0] / 65536, got[1] / 65536, got[2] / 65536, got[3] & 0xFFFFFFFF, mo[0] / 65536,
                        mo[1] / 65536, mo[2] / 65536, mo[3] & 0xFFFFFFFF))
        print('  %d of %d differ' % (bad, min(len(cs), len(model))))


if __name__ == '__main__':
    main()

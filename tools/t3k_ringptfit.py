"""Tempest 3000 point rings (entry type 00310008, module 806A5FB0 mode 0, handler 806A6486): the point list vs the
emulator's calls of the module's disc routine (local 20300BA8).

python -I tools/t3k_ringptfit.py <pclog span file> <entrylog>
Logs: python -I tools/t3k_logrun.py --state play_f4600 --fields 12 --pc 20300BA8 --interp 806A5F00-806A6B00 --entries
      --mem 20100B40 --out <prefix>

Model (806A6486..806A66D6, literal): depth z from $3000000 - (w1 lo x w10 >> 16), down by w10 while > 0; per depth
w11 copies of an arc (start angle w6 + (w4 hi x f(p1)), span w9 lo + (w5 hi x f(p2)), the phases p1 / p2 starting at
w1 hi x w4 lo / w5 lo and stepping back by w4 lo / w5 lo per depth; copies 2^16 / w11 apart) of w8 points, colours
along a 2-texel gradient (2 words at w2; u steps 2^17 / w8), each projected with k = 2^44 / z about (180.5, 120.5):
centre = R(angle) x (w9 x k >> 20), size = 12 x k >> 21 (none below 1, at most 30), intensity (min(w7 hi x k >> 20,
$3FFF) << 16) | w7 lo.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import t3k_spanlib as sl  # noqa: E402
from t3k_flowerfit import mul, sincos  # noqa: E402

ORDER = int(os.environ.get('RP_ORDER', '0'))   # which sincos output the module multiplies (test)


def sc(a):
    c, s = sincos(a & 0xFFFF)
    return (c, s) if ORDER == 0 else (s, c)


def points(w, mem):
    out = []
    cols = sl.read_block(mem, w[2], 2)
    w1, w4, w5 = w[1], w[4], w[5]
    w10, w11, w8 = sl.s32(w[10]), sl.s32(w[11]), sl.s32(w[8])
    if w11 <= 0 or w8 <= 0 or w10 <= 0:
        return out
    z = 0x3000000 - ((w1 & 0xFFFF) * w10 >> 16)
    hi1 = sl.s32(w1) >> 16
    p1 = (w4 & 0xFFFF) * hi1
    p2 = (w5 & 0xFFFF) * hi1
    step_copy = (1 << 16) // w11
    while z > 0:
        f2 = sc(p2)[0]
        span = sl.lo16(w[9]) + mul(w5 >> 16, f2, 30)
        f1 = sc(p1)[0]
        a0 = sl.s32(w[6]) + mul(w4 >> 16, f1, 30)
        p1 -= w4 & 0xFFFF
        p2 -= w5 & 0xFFFF
        for copy in range(w11):
            k = int(2 ** 44 / z)
            n = w8
            step = mul((1 << 28) // n, span, 28)
            ustep = ((1 << 28) // n) >> 11
            I = (min(mul(w[7] >> 16, k, 20), 0x3FFF) << 16) | (w[7] & 0xFFFF)
            radius = mul(sl.s32(w[9]), k, 20)
            size = mul(12, k, 21)
            a = a0 + copy * step_copy - (span >> 1)
            if size >= 1:
                size = min(size, 30)
                u = 0
                for i in range(n):
                    c, s = sc(a)
                    x = mul(radius, c, 30) + 0xB48000
                    y = mul(radius, s, 30) + 0x788000
                    out.append((x - (size << 15), y - (size << 15), size, I, cols, u))
                    u += ustep
                    a += step
        z -= w10
    return out


def main():
    pcl, entlog = sys.argv[1], sys.argv[2]
    ents = sl.load_entries(entlog, 0x00310008)
    calls = {}
    for line in open(pcl):
        if line.startswith('pc '):
            ev = sl.parse_pc(line.split())
            calls.setdefault(ev.picture, []).append(ev)
    for pic, cs in sorted(calls.items())[:3]:
        if pic not in ents:
            continue
        w, mem = ents[pic][0]
        model = points(w, mem)
        print('picture %d: %d calls, model %d points' % (pic, len(cs), len(model)))
        bad = 0
        for i, (ev, mo) in enumerate(zip(cs, model)):
            r = ev.r
            got = (sl.s32(r[4]), sl.s32(r[5]), r[6] >> 16, r[13])
            exp = (mo[0], mo[1], mo[2], mo[3])
            if any(abs(p - q) > 64 for p, q in zip(got[:2], exp[:2])) or got[2:] != exp[2:]:
                bad += 1
                if bad <= 4:
                    print('  point %d: log x %.3f y %.3f size %d I %08X | model x %.3f y %.3f size %d I %08X' % (
                        i, got[0] / 65536, got[1] / 65536, got[2], got[3], exp[0] / 65536, exp[1] / 65536, exp[2], exp[3]))
        print('  %d of %d differ' % (bad, min(len(cs), len(model))))


if __name__ == '__main__':
    main()

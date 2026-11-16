"""Tempest 3000 title dot field (entry type 0031000E, module 806AFA80 mode 0, handler 806AFECA): the disc list vs the
emulator's calls of the module's disc routine (local 20300BC8; r4 / r5 top-left 16.16, r6 size, r13 intensity) and
the colour pixel at local $B40.

python -I tools/t3k_fieldfit.py <pclog span file> <entrylog>
Logs: python -I tools/t3k_logrun.py --state title_f1000 --fields 8 --pc 20300BC8 --interp 806AFA00-806B0800 --entries
      --mem 20100B40 --out <prefix>

Model (806AFECA..806B0078 + 806B007A..806B01BE, read literally):
  camera (cx, cy) = -2 x the words at $800DC108 / $800DC10C; hi1 = w1 >> 16 (signed); p1 = w4 lo x hi1, p2 = w5 lo x hi1;
  z = $3000000 - (w1 lo x w10 >> 16); while z > 0:
    b = w5 hi x cos(p2) >> 30, a = w4 hi x cos(p1) >> 30 (w4 / w5 hi unsigned); p1 -= w4 lo, p2 -= w5 lo
    if (a x a >> 16) + (b x b >> 16) >= $2000:            (else nothing at this depth)
      colour = pixel (Y $1FFF0000, Cr a << 12, Cb b << 12) - st_p at $B40
      rx = w12 x (b << 7) >> 16, ry = w12 x (a << 7) >> 16;  s = $20000
      w13 times (zz = z, then zz = (zz - w14) & $7FFFFFF):
        ring(zz, s); s = s x w3 >> 16
    z -= w10
  ring(zz, s): k = 2^44 / zz; I = (min(w7 hi x s >> 16 x k >> 20, $3FFF) << 16) | w7 lo; size = $C0000 x k >> 21,
    none below 1 px, at most 30; (Rx, Ry) = (rx, ry) x k >> 20; camera x k >> 20;
    w11 points at angle w6 + i x (w9 x (2^16 / w11) >> 16): (x, y) = (Rx cos - Ry sin, Rx sin + Ry cos) >> 30,
    each drawn at (x, y) and (-x, y): + camera + (180.5, 120.5) - size / 2.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import t3k_spanlib as sl  # noqa: E402
from t3k_flowerfit import mul, sincos  # noqa: E402


def field(w, mem):
    """[(x, y, size, I, colour regs (y, cr, cb))] in call order (16.16 top-left)."""
    out = []
    cam_y, cam_x = mem.get(0x800DC108, 0), mem.get(0x800DC10C, 0)
    cx0, cy0 = -2 * sl.s32(cam_x), -2 * sl.s32(cam_y)
    w1, w4, w5 = w[1], w[4], w[5]
    hi1 = sl.s32(w1) >> 16
    p1 = (w4 & 0xFFFF) * hi1
    p2 = (w5 & 0xFFFF) * hi1
    w9, w10, w11 = sl.s32(w[9]), sl.s32(w[10]), sl.s32(w[11])
    w12, w13, w14, w3 = sl.s32(w[12]), sl.s32(w[13]), sl.s32(w[14]), sl.s32(w[3])
    if w10 <= 0 or w11 <= 0 or w13 <= 0:
        return out
    z = 0x3000000 - ((w1 & 0xFFFF) * w10 >> 16)
    step_copy = (1 << 16) // w11
    guard = 0
    while z > 0 and guard < 4096:
        guard += 1
        b = mul(sincos(p2 & 0xFFFF)[0], w5 >> 16, 30)
        a = mul(sincos(p1 & 0xFFFF)[0], w4 >> 16, 30)
        p1 -= w4 & 0xFFFF
        p2 -= w5 & 0xFFFF
        if (a * a >> 16) + (b * b >> 16) >= 0x2000:
            colour = (0x1FFF0000, sl.s32((a << 14) & 0xFFFFFFFF) >> 2, sl.s32((b << 14) & 0xFFFFFFFF) >> 2)
            rx = mul(w12, sl.s32((b << 7) & 0xFFFFFFFF), 16)
            ry = mul(w12, sl.s32((a << 7) & 0xFFFFFFFF), 16)
            s = 0x20000
            zz = z
            for _ in range(w13):
                if zz > 0:
                    k = int(2 ** 44 / zz)
                    I = (min(mul(mul(s, w[7] >> 16, 16), k, 20), 0x3FFF) << 16) | (w[7] & 0xFFFF)
                    size = mul(k, 0xC0000, 21)
                    if size >= 0x10000:
                        size = min(size, 0x1E0000)
                        RX, RY = mul(k, rx, 20), mul(k, ry, 20)
                        ccx, ccy = mul(k, cx0, 20), mul(k, cy0, 20)
                        ang = sl.s32(w[6])
                        step = mul(w9, step_copy, 16)
                        for _i in range(w11):
                            c, sn = sincos(ang & 0xFFFF)
                            x = sl.s32(mul(RX, c, 30) - mul(RY, sn, 30))
                            y = sl.s32(mul(RX, sn, 30) + mul(RY, c, 30))
                            for xx in (x, -x):
                                px = xx + ccx + 0xB48000 - (size >> 1)
                                py = y + ccy + 0x788000 - (size >> 1)
                                out.append((px, py, size, I, colour))
                            ang += step
                s = mul(s, w3, 16)
                zz = (zz - w14) & 0x7FFFFFF
        z -= w10
    return out


def main():
    pcl, entlog = sys.argv[1], sys.argv[2]
    ents = sl.load_entries(entlog, 0x0031000E)
    calls = {}
    for line in open(pcl):
        if line.startswith('pc '):
            ev = sl.parse_pc(line.split())
            calls.setdefault(ev.picture, []).append(ev)
    for pic, cs in sorted(calls.items()):
        if pic not in ents:
            continue
        w, mem = ents[pic][0]
        print('entry', ' '.join('%08X' % x for x in w), '| cam', '%08X %08X' % (mem.get(0x800DC108, 0), mem.get(0x800DC10C, 0)))
        model = field(w, mem)
        print('picture %d: %d calls, model %d discs' % (pic, len(cs), len(model)))
        bad = 0
        for i, (ev, mo) in enumerate(zip(cs, model)):
            r = ev.r
            got = (sl.s32(r[4]), sl.s32(r[5]), r[6], r[13])
            exp = (mo[0], mo[1], mo[2], mo[3])
            if any(abs(p - q) > 64 for p, q in zip(got[:2], exp[:2])) or got[2:] != exp[2:]:
                bad += 1
                if bad <= 5:
                    print('  disc %d: log x %.3f y %.3f size %.3f I %08X | model x %.3f y %.3f size %.3f I %08X' % (
                        i, got[0] / 65536, got[1] / 65536, got[2] / 65536, got[3], exp[0] / 65536, exp[1] / 65536,
                        exp[2] / 65536, exp[3] & 0xFFFFFFFF))
        print('  %d of %d differ; extra fields of the first call:' % (bad, min(len(cs), len(model))), cs[0].extra[:6])


if __name__ == '__main__':
    main()

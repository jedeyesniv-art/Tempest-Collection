"""Tempest 3000 disc shapes (entry type 0031020E, module 806AFA80 mode 2, handler 806AFC52): the disc list vs the
emulator's calls of the module's disc routine (local 20300BC8).

python -I tools/t3k_flowerfit.py <pclog span file> <entrylog>
Logs: python -I tools/t3k_logrun.py --state play_f2880 --fields 12 --pc 20300BC8 --interp 806AFA00-806B0800 --entries
      --mem 20100B40 --out <prefix>

Model (806AFC52..806AFEC8, read literally): w10 -> a header (4 words: palette pointer, r16, r17, -) and 64 packed
records after it; palette = 16 colours. Per record (byte3 angle, byte2 signed radius, bits 8-14 size, 4-7 intensity,
0-3 colour; size 0 ends the list): a = byte3 x 256 x r16 >> 16, radius = (sext(byte2) << 10) x w1 >> 16 x w12 >> 15,
intensity = (bits4-7 << 13) x ((256 - w1) << 21) >> 16; loop (n = r17 & $FFFF, m = r17 >> 16): draw(a, b); a += 2^16 / n;
n -= 1; while n > 0 repeat; then b += 2^16 / m; m -= 1; while m > 0 repeat (n stays spent: one more disc per m).
draw: (x, y) = rot(a) applied as (r (cos - sin), r (sin + cos)) >> 14; tilt by (b + (w1 << 6)): depth = x sin, x = x cos;
rotate by w14 + $2000; k = 2^44 / max(w9 + depth x 4, $A0000); x, y x k >> 19 (+ w0 x hi / y lo); size = r29 x k >> 19
+ 1 px, at most 8 px; top-left = position - size / 2.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import t3k_spanlib as sl  # noqa: E402

# the vector module's sine (806A4BBE), the same constants as 806B05D6
C1, C2, C3, C4 = -0x46FE51 - 1 + 0, 0x051573CA, -0x29564EF6, 0x6487D705
C1 = sl.s32(0xFFB901AF)
C3 = sl.s32(0xD6A9B10A)


def mul(a, b, sh):
    return sl.s32((a * b) >> sh)


def sincos(angle):
    x = sl.s32(((angle << 18) & 0xFFFFFFFF) >> 2)
    q = (angle >> 14) & 3
    if x == 0:
        cc, ss = 0x40000000, 0
    else:
        one_x = sl.s32(0x40000000 - x)
        u, v = mul(x, x, 30), mul(one_x, one_x, 30)
        pu, pv = sl.s32(mul(u, C1, 30) + C2), sl.s32(mul(v, C1, 30) + C2)
        pu, pv = sl.s32(mul(u, pu, 30) + C3), sl.s32(mul(v, pv, 30) + C3)
        pu, pv = sl.s32(mul(u, pu, 30) + C4), sl.s32(mul(v, pv, 30) + C4)
        ss, cc = mul(x, pu, 30), mul(one_x, pv, 30)
    return [(cc, ss), (-ss, cc), (-cc, -ss), (ss, -cc)][q]


def recip_bits(x, frac):
    """2^(28+frac) / x as the module normalises it (float)."""
    return int(2 ** (28 + frac) / x) if x > 0 else 0


def discs(w, mem):
    hdr = sl.read_block(mem, w[10], 4)
    pal = sl.read_block(mem, hdr[0], 16)
    r16, r17 = hdr[1], hdr[2]
    recs = sl.read_block(mem, w[10] + 16, 64)
    m0, n0 = r17 >> 16, r17 & 0xFFFF
    step_b = (1 << 16) // m0 if m0 else 0
    step_a = (1 << 16) // n0 if n0 else 0
    w1 = sl.s32(w[1])
    out = []
    for rec in recs:
        size_bits = (rec >> 8) & 0x7F
        if size_bits == 0:
            break
        r29 = size_bits << 16
        a = mul(((rec >> 24) & 0xFF) << 8, r16, 16)
        r28 = mul(mul(sl.s32(((rec >> 16) & 0xFF) << 24) >> 24 << 10, w1, 16), sl.s32(w[12]), 15)
        inten = mul(((rec >> 4) & 15) << 13, ((0x100 - w1) << 21) & 0xFFFFFFFF if False else (0x100 - w1) << 21, 16)
        col = pal[rec & 15]
        b, n, m = 0, n0, m0
        while True:
            c, s = sincos(a & 0xFFFF)
            x = mul(r28, c, 14) + mul(r28, -s, 14)
            y = mul(r28, s, 14) + mul(r28, c, 14)
            c2, s2 = sincos((b + (w1 << 6)) & 0xFFFF)
            depth = mul(x, s2, 30)
            x = mul(c2, x, 30)
            c3, s3 = sincos((w[14] + 0x2000) & 0xFFFF)
            x, y = mul(x, c3, 30) + mul(y, -s3, 30), mul(x, s3, 30) + mul(y, c3, 30)
            d = max(sl.s32(w[9]) + (depth << 2), 0xA0000)
            k = recip_bits(d, 16)
            x, y = mul(k, x, 19), mul(k, y, 19)
            size = min(mul(r29, k, 19) + 0x10000, 0x80000)
            x += sl.hi16(w[0]) << 16
            y += sl.lo16(w[0]) << 16
            out.append((x - (size >> 1), y - (size >> 1), size, inten, col))
            a += step_a
            n -= 1
            if n > 0:
                continue
            b += step_b
            m -= 1
            if m > 0:
                continue
            break
    return out


def main():
    pcl, entlog = sys.argv[1], sys.argv[2]
    ents = sl.load_entries(entlog, 0x0031020E)
    calls = {}
    for line in open(pcl):
        if line.startswith('pc '):
            ev = sl.parse_pc(line.split())
            calls.setdefault(ev.picture, []).append(ev)
    for pic, cs in sorted(calls.items()):
        if pic not in ents:
            continue
        for w, mem in ents[pic][:1]:
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

"""Tempest 3000 dot images (entry type 00310209, module 806A8990 mode 2, handler 806A8DB4): the dot list vs the
emulator's calls of the disc routine 806A9124.

python -I tools/t3k_dotfit.py <pclog span file> <entrylog>
Logs: python -I tools/t3k_logrun.py --state play_f2880 --fields 12 --pc 2030088C --interp 806A8900-806A9800 --entries
      --mem 20100B40 --out <prefix>     (2030088C = the jsr to the disc routine: r4 / r5 top-left 16.16, r6 size,
      r13 intensity, $B40 the colour)

Model (806A8DB4..806A8F8C): copies = w13 & $7F (at least the first); per copy: k from depth (w4 at first, minus w5 per
copy; stop when <= 0) as the particles (0.75 x 2^44 / (depth + $800000)); scale x = -w2 x k >> 19 + copies_left x
w11 ..., centre (w0 x / 2 x k >> 19 + 180, y + 120) in whole pixels; an image of w8 (columns hi, rows lo) YCrCbZ
pixels at w12 (w13 bit 7: w12 is a list of image pointers, one per copy), rows top down, columns right to left
counters; pixel Z = 0 skipped; dot intensity = (Z << 22) x w6 >> 16; angle = w1 lo (+ w1 hi per copy); dot size w9.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import t3k_spanlib as sl  # noqa: E402


def recip_k(depth):
    return int(0xC000 * (2 ** 44 / (depth + 0x800000)) / 65536)


def sincos(a):
    """806A95B2: angle 16 bits a turn -> (cos, sin) 2.30 (float model)."""
    t = (a & 0xFFFF) * 2 * math.pi / 65536
    return int(round(math.cos(t) * 2 ** 30)), int(round(math.sin(t) * 2 ** 30))


def mul(a, b, sh):
    return (a * b) >> sh


def dots(w, mem):
    out = []
    w13 = w[13]
    lst = w[12] if w13 & 0x80 else 0
    copies = (w13 & 0x7F) or 1
    depth = sl.s32(w[4])
    angle = w[1]
    n = copies
    while True:
        k = recip_k(depth)
        x0, y0 = sl.hi16(w[0]) >> 1, sl.lo16(w[0]) >> 1
        img = w[12]
        if lst:
            img = mem.get(lst, 0)
            lst += 4
        s16 = mul(k, -sl.s32(w[2]), 19)
        s17 = mul(k, -sl.s32(w[3]), 19)
        cx = mul(k, x0, 19) + 0xB4
        cy = mul(k, y0, 19) + 0x78
        grow = n * sl.s32(w[11])
        s16 += grow
        s17 += grow
        cols, rows = w[8] >> 16, w[8] & 0xFFFF
        r2 = mul(grow, sl.s16(w[14] >> 16), 16)
        r3 = mul(grow, sl.s16(w[14]), 16)
        r10 = -(((cols >> 1) << 16) + r2)
        r11 = -(((rows >> 1) << 16) + r3)
        c, s = sincos(angle)
        size = w[9]
        pix_addr = img
        for row in range(rows, 0, -1):
            for col in range(cols, 0, -1):
                p = mem.get(pix_addr, 0)
                pix_addr += 4
                z = p & 0xFF
                if z == 0:
                    continue
                I = mul(z << 22, w[6] & 0xFFFFFFFF, 16)
                x = mul(s16, r10 + (col << 16), 16)
                y = mul(s17, r11 + (row << 16), 16)
                xr = mul(x, c, 30) + mul(y, -s, 30) + (cx << 16)
                yr = mul(x, s, 30) + mul(y, c, 30) + (cy << 16)
                out.append((xr, yr, size, I, p))
        n -= 1
        angle = (angle + (sl.s16(angle >> 16) if False else (sl.s32(angle) >> 16))) & 0xFFFFFFFF
        depth -= sl.s32(w[5])
        if depth <= 0 or n == 0:
            break
    return out


def main():
    pcl, entlog = sys.argv[1], sys.argv[2]
    ents = sl.load_entries(entlog, 0x00310209)
    calls = {}
    for line in open(pcl):
        if line.startswith('pc '):
            ev = sl.parse_pc(line.split())
            if ev.pc == 0x2030088C:
                calls.setdefault(ev.picture, []).append(ev)
    for pic, cs in sorted(calls.items()):
        for ofs in (0, -1, 1):
            if pic + ofs in ents:
                break
        w, mem = ents[pic + ofs][0]
        model = dots(w, mem)
        print('picture %d: %d calls, model %d dots (entry picture %d)' % (pic, len(cs), len(model), pic + ofs))
        bad = 0
        for i, (ev, m) in enumerate(zip(cs, model)):
            r = ev.r
            got = (sl.s32(r[4]), sl.s32(r[5]), r[6] >> 16, r[13])
            exp = (m[0], m[1], m[2], m[3])
            if any(abs(a - b) > 2 for a, b in zip(got, exp)):
                bad += 1
                if bad <= 5:
                    print('  dot %d: log x %.3f y %.3f size %d I %08X | model x %.3f y %.3f size %d I %08X' % (
                        i, got[0] / 65536, got[1] / 65536, got[2], got[3], exp[0] / 65536, exp[1] / 65536, exp[2], exp[3]))
        print('  %d of %d differ' % (bad, min(len(cs), len(model))))


if __name__ == '__main__':
    main()

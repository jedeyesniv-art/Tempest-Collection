"""Tempest 3000 vector shapes (entry type 00710001, renderer module 806A3F50): a bit-exact model of the segment
transform, checked against the emulator.

python -I tools/t3k_vecmodel.py <pclog>

<pclog>: NUANCE_NOJIT=1 NUANCE_NOAOT=1 NUANCE_THREADS=0 NUANCE_PCLOG=2030072C
NUANCE_PCLOG_MEM=20100E60,20100CE0,20100B90 NUANCE_JOBLOG=<file> - MPE0's registers where both endpoints of a segment are
final 16.16 screen coordinates (r8, r9) - (r10, r11), the entry at $20100030, the vertices (local $20100E60: current,
previous), the jitter LFSR state ($20100CEC, already stepped) and the object matrix ($20100B90).

Entry words used (local $20100030 + 4n): w0 = screen position (x hi, y lo), w4 = sizes (10-bit signed halves, 8.8 of
the shape's units: x scale hi, y scale lo), w5 = width etc. (shape commands 0 / 5 change it), w6 = angle (16 bits a
turn), w7 = shape stream, w12 = jitter strength (+ the stream's CMD8 / 9 offset), w13 = jitter amounts (bytes:
angle, x, y).
"""
import sys

M32 = 0xFFFFFFFF


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def mul(a, b, sh):
    """NUON mul a, b, >>#sh: signed product, arithmetic shift, low 32 bits."""
    return s32((s32(a) * s32(b)) >> sh)


def sext(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


C1, C2, C3, C4 = s32(0xFFB901AF), 0x051573CA, s32(0xD6A9B10A), 0x6487D705


def sincos(angle):
    """Library 806A4BBE (local $20300F3E): angle (16 bits a turn) -> (r0, r1) = (cos, sin) in 2.30."""
    x = ((angle << 18) & M32) >> 2          # lsr: the quadrant fraction, 2.30
    q = (angle >> 14) & 3
    if x == 0:
        c, s = 0x40000000, 0
    else:
        one_x = s32(0x40000000 - x)
        u = mul(x, x, 30)
        v = mul(one_x, one_x, 30)
        p = C1
        pu = s32(mul(u, p, 30) + C2)
        pv = s32(mul(v, p, 30) + C2)
        pu = s32(mul(u, pu, 30) + C3)
        pv = s32(mul(v, pv, 30) + C3)
        pu = s32(mul(u, pu, 30) + C4)
        pv = s32(mul(v, pv, 30) + C4)
        s = mul(x, pu, 30)
        c = mul(one_x, pv, 30)
    if q == 0:
        return c, s
    if q == 1:
        return s32(-s), c
    if q == 2:
        return s32(-c), s32(-s)
    return s, s32(-c)


def matrix(w4, w6):
    """Entry set-up (806A406A..806A40BC): the object matrix r20..r23 (stored at $20100B90)."""
    s_lo = sext(w4, 10) << 8
    s_hi = sext(w4 >> 16, 10) << 8
    a, b = sincos(w6 & 0xFFFF)
    return mul(s_hi, a, 16), mul(s_lo, s32(-b), 16), mul(s_hi, b, 16), mul(s_lo, a, 16)


def segment(e, cur, prev, lfsr, cf0=0):
    """806A422A..806A43A8: one line segment prev -> cur. Returns (x0, y0, x1, y1) 16.16, sorted so y0 <= y1."""
    r20, r21, r22, r23 = matrix(e[4], e[6])
    r8, r9 = sext(cur, 16), sext(cur >> 16, 16)
    r10, r11 = sext(prev, 16), sext(prev >> 16, 16)
    r28 = r8 + ((r10 - r8) >> 1)
    r29 = r9 + ((r11 - r9) >> 1)
    r8, r9, r10, r11 = r8 - r28, r9 - r29, r10 - r28, r11 - r29
    j16 = sext(lfsr >> 16, 8)
    j17 = sext(lfsr >> 24, 8)
    t = s32(e[12] + cf0)
    t = 0 if t <= 0 else min(t, 0xFFFF)
    w13 = e[13]
    r0 = mul(lfsr & 0xFFFF, sext(w13 >> 24, 8), 4)
    j16 = mul((w13 >> 16) & 0xFF, j16, 5)
    j17 = mul((w13 >> 8) & 0xFF, j17, 5)
    j16 = mul(t, j16, 16)
    j17 = mul(t, j17, 16)
    r0 = mul(t, r0, 16)
    c, s = sincos(r0 & 0xFFFF)
    r28 += j16
    r29 += j17
    def rot(x, y):
        return s32(mul(x, c, 30) + mul(y, s32(-s), 30)), s32(mul(x, s, 30) + mul(y, c, 30))
    r8, r9 = rot(r8, r9)
    r10, r11 = rot(r10, r11)
    r8, r9, r10, r11 = r8 + r28, r9 + r29, r10 + r28, r11 + r29
    def obj(x, y):
        return s32(mul(r20, x, 14) + mul(r21, y, 14)), s32(mul(r22, x, 14) + mul(r23, y, 14))
    r8, r9 = obj(r8, r9)
    r10, r11 = obj(r10, r11)
    ox, oy = s32((e[0] >> 16) << 16), s32((e[0] & 0xFFFF) << 16)
    r8, r9, r10, r11 = s32(r8 + ox), s32(r9 + oy), s32(r10 + ox), s32(r11 + oy)
    if r11 < r9:
        r8, r9, r10, r11 = r10, r11, r8, r9
    return r8 & M32, r9 & M32, r10 & M32, r11 & M32


def main():
    n = ok = mat_ok = 0
    bad = []
    for line in open(sys.argv[1]):
        t = line.split()
        if len(t) < 68 or t[0] != 'pc' or t[52] != '00710001':
            continue
        r = [int(x, 16) for x in t[4:36]]
        e = [int(x, 16) for x in t[37:53]]
        mem = {}
        i = 53
        while i < len(t):
            mem[int(t[i][1:], 16)] = [int(x, 16) for x in t[i + 1:i + 5]]
            i += 5
        cur, prev = mem[0x20100E60][0], mem[0x20100E60][1]
        lfsr = mem[0x20100CE0][3]
        n += 1
        if list(matrix(e[4], e[6])) == [s32(v) for v in mem[0x20100B90]]:
            mat_ok += 1
        got = segment(e, cur, prev, lfsr)
        want = (r[8], r[9], r[10], r[11])
        if got == want:
            ok += 1
        elif len(bad) < 8:
            bad.append((t[1], ['%08X' % v for v in want], ['%08X' % v for v in got], '%08X %08X' % (cur, prev),
                        'w4 %08X w6 %08X w12 %08X w13 %08X lfsr %08X' % (e[4], e[6], e[12], e[13], lfsr)))
    print('%d segments: matrix exact %d, endpoints exact %d' % (n, mat_ok, ok))
    for b in bad:
        print(*b)


if __name__ == '__main__':
    main()

"""Model of the T3K jump burst (display list type 00310108 with w4 bit 0 set; module 806A5FB0 mode 1, handler
806A630A) checked against a NUANCE_PCLOG run of the module.

python -I tools/t3k_jumpburstfit.py <pc108.txt>

The log lines come from tools/t3k_logrun.py --pc 20300682,203006AA,203006FC,20300752 --interp 806A5FB0-806A6B00
--mem 20100B40,20100B60,20100B64,20100B98,20100B9C,20100BA0, filtered to entry w15 = 00310108. Per particle:
  - four LFSR steps (806A66D8: r >>= 1, and ^= $A3000000 when the bit shifted out was 0) from seeds r8 / r9 / r10
    ($23956329 / $49352850 / $0946354F, set at module entry: the same 96 particles every picture)
  - x, y = the first two as signed 24-bit, z = (third - 2 w3) & $1FFFFFF, size = fourth & 15
  - x, y x z >> 26, size x z >> 25; colour = lerp($D2921000, mem[$8010B154]) by z << 7 (2.30)
  - projection as 00310309: k = $C000 x recip(w2 + cam z + z + $800000), pos = ((w0 + x) / 2 + cam x 8) x k >> 19
"""
import sys


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def lfsr(r):
    out = r >> 1
    if not (r & 1):
        out ^= 0xA3000000
    return out


def mul(a, b, sh):
    return s32((s32(a) * s32(b)) >> sh)


def main():
    rows = [l.split() for l in open(sys.argv[1])]
    by = {}
    for t in rows:
        by.setdefault(t[3], []).append(t)
    r8, r9, r10 = 0x23956329, 0x49352850, 0x0946354F
    bad = 0
    for n in range(96):
        a = by['20300682'][n]
        e = [int(x, 16) for x in a[38:54]]
        reg = lambda t, i: int(t[5 + i], 16)
        assert (reg(a, 8), reg(a, 9), reg(a, 10)) == (r8, r9, r10), n
        r8 = lfsr(r8); x = r8
        r9 = lfsr(r9); y = r9
        r10 = lfsr(r10); z = r10
        r8 = lfsr(r8); sz = r8 & 15
        b = by['203006AA'][n]
        assert (reg(b, 21), reg(b, 22), reg(b, 23), reg(b, 16)) == (x, y, z, r8), n   # (r16 masked at this pc)
        x = s32((x << 8) & 0xFFFFFFFF) >> 8
        y = s32((y << 8) & 0xFFFFFFFF) >> 8
        z = (z - (e[3] << 1)) & 0x1FFFFFF
        X, Y, S = mul(z, x, 26), mul(z, y, 26), mul(z, sz, 25)
        c = by['203006FC'][n]
        got = (s32(reg(c, 21)), s32(reg(c, 22)), reg(c, 23), reg(c, 16))
        if got != (X, Y, z, S):
            bad += 1
            print('xyzs', n, got, (X, Y, z, S))
        # colour: logged word at $20100B40 (after st_p)
        mi = c.index('m20100B40')
        col = int(c[mi + 1], 16)
        A = 0xD2921000
        B = int(c[c.index('m20100B64') + 1], 16)
        t = ((z << 7) & 0xFFFFFFFF) / float(1 << 30)
        pred = []
        for sh in (24, 16, 8):
            ca, cb = (A >> sh) & 0xFF, (B >> sh) & 0xFF
            pred.append((ca, cb))
        # mul_p takes ru's fraction (low 16 bits): t = ((z << 7) & $FFFF) / 2^16, per byte A + (B - A) t
        f = (z << 7) & 0xFFFF
        pc = 0
        for sh in (24, 16, 8):
            ca, cb = (A >> sh) & 0xFF, (B >> sh) & 0xFF
            pc |= ((ca + (((cb - ca) * f) >> 16)) & 0xFF) << sh
        if pc != col:
            bad += 1
            print('colour', n, '%08X model %08X' % (col, pc))
        d = by['20300752'][n]
        r14, r15, r16, r30 = s32(reg(d, 14)), s32(reg(d, 15)), s32(reg(d, 16)), s32(reg(d, 30))
        if n < 6:
            print('   proj r14 %d r15 %d r16 %d k %d' % (r14, r15, r16, r30))
    print('mismatches', bad)


main()

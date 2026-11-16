"""Tempest 3000 title wave (entry type 00310409, module 806A8990 mode 4, handler 806A8C48): the disc list vs the
emulator's calls of the blob routine (local 20300A64; r4 / r5 top-left 16.16, r6 size, r13 intensity) and the colour
pixel at local $B40.

python -I tools/t3k_wavefit.py <pclog span file> <entrylog>
Logs: python -I tools/t3k_logrun.py --state title_f1000 --fields 8 --pc 20300A64 --interp 806A8900-806A9800 --entries
      --mem 20100B40 --out <prefix>

Model (806A8C48..806A8DB0, read literally; a fixed shape at w0):
  colours: 2 words at $8010DB60 (wave, circle); x0 = w0 hi << 16, y0 = w0 lo << 16; size 8 px; I = (1.0 - w8) << 12.
  96 wave dots, a = $4000 + i x $400, (c, s) = sincos(a), xs = 0 (16.16, grows 1 px a straight dot):
    a >= $18000: x = xs - 18 s, y = 9 + 18 c        (the closing arc)
    a >= $8000:  x = xs, y = 9 c; xs += 1 px        (the wave)
    else:        x = -18 s, y = 9 + 18 c            (the opening arc)
    jitter: lfsr steps (r8, then r9; from $23956329 / $49352850; lfsr >> 1, ^ $A3000000 when bit 0 was 0),
    jx = (r8 asr 10) x w8 >> 16, jy = (r9 asr 10) x w8 >> 16; top-left = (x0 + x + jx, y0 + y + jy).
  32 circle dots, a = w8 >> 2 + i x $800, R = $80000 + (w8 << 6): top-left = (x0 + 53 px + R s, y0 - 4 px + R c).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import t3k_spanlib as sl  # noqa: E402
from t3k_flowerfit import mul, sincos  # noqa: E402


def step(v):
    return v >> 1 if v & 1 else (v >> 1) ^ 0xA3000000


def wave(w, mem):
    out = []
    x0 = (w[0] >> 16) << 16
    y0 = (w[0] & 0xFFFF) << 16
    w8 = sl.s32(w[8])
    I = ((0x10000 - w8) << 12) & 0xFFFFFFFF
    r8, r9 = 0x23956329, 0x49352850
    xs = 0
    a = 0x4000
    for _ in range(96):
        c, s = sincos(a & 0xFFFF)
        if a >= 0x18000:
            x, y = xs - mul(0x120000, s, 30), 0x90000 + mul(0x120000, c, 30)
        elif a >= 0x8000:
            x, y = xs, mul(0x90000, c, 30)
            xs += 0x400 << 6
        else:
            x, y = -mul(0x120000, s, 30), 0x90000 + mul(0x120000, c, 30)
        r8 = step(r8)
        jx = mul(sl.s32(r8) >> 10, w8, 16)
        r9 = step(r9)
        jy = mul(sl.s32(r9) >> 10, w8, 16)
        out.append((sl.s32(x0 + x + jx), sl.s32(y0 + y + jy), 0x80000, I, 0))
        a += 0x400
    a = w8 >> 2
    R = 0x80000 + (w8 << 6)
    for _ in range(32):
        c, s = sincos(a & 0xFFFF)
        out.append((sl.s32(x0 + 0x350000 + mul(R, s, 30)), sl.s32(y0 - 0x40000 + mul(R, c, 30)), 0x80000, I, 1))
        a += 0x800
    return out


def main():
    pcl, entlog = sys.argv[1], sys.argv[2]
    ents = sl.load_entries(entlog, 0x00310409)
    calls = {}
    for line in open(pcl):
        if line.startswith('pc ') and ' 20300A64 ' in line:
            ev = sl.parse_pc(line.split())
            calls.setdefault(ev.picture, []).append(ev)
    for pic, cs in sorted(calls.items()):
        if pic not in ents:
            continue
        w, mem = ents[pic][0]
        model = wave(w, mem)
        print('picture %d: w0 %08X w8 %08X; %d calls, model %d' % (pic, w[0], w[8], len(cs), len(model)))
        bad = 0
        for i, (ev, mo) in enumerate(zip(cs, model)):
            got = (sl.s32(ev.r[4]), sl.s32(ev.r[5]), ev.r[6], ev.r[13])
            if any(abs(p - q) > 64 for p, q in zip(got[:2], mo[:2])) or got[2:] != mo[2:4]:
                bad += 1
                if bad <= 5:
                    print('  dot %d: log %.3f %.3f %X %08X | model %.3f %.3f %X %08X' % (
                        i, got[0] / 65536, got[1] / 65536, got[2], got[3], mo[0] / 65536, mo[1] / 65536, mo[2], mo[3]))
        print('  %d of %d differ; colour pixel at the first / last call:' % (bad, min(len(cs), len(model))),
              cs[0].extra[1:2], cs[-1].extra[1:2], '| colours at $8010DB60:',
              '%08X %08X' % (mem.get(0x8010DB60, 0), mem.get(0x8010DB64, 0)))


if __name__ == '__main__':
    main()

"""Tempest 3000: does a drawing module blend Y, Cr and Cb alike? Per written pixel, the blend factor per channel.

python -I tools/t3k_chromafit.py <spanlog> [type]

From a NUANCE_SPANLOG run (tools/t3k_logrun.py --span <type>): 'span rd' / 'span wr' pairs of the same row give the
destination before (d) and after (o). The drawn colour c is unknown per pixel, so the test is c-free: if every channel
blends by the same k, then (o - d) is k x (c - d) for all three. For entries with one colour (w2 == w3, lines without
a gradient), c is known: k per channel = (o - d) / (c - d), compared where |c - d| > 24 in all three channels.
Prints the mean k per channel, and the mean of k_Cr / k_Y and k_Cb / k_Y (1 = alike).
"""
import sys


def chans(p):
    return (p >> 24) & 0xFF, (p >> 16) & 0xFF, (p >> 8) & 0xFF


def main():
    entry = None
    rd = {}
    rows = []   # (colour, d, o)
    for line in open(sys.argv[1]):
        t = line.split()
        if not t:
            continue
        if t[0] == 'spanentry':
            w = [int(x, 16) for x in t[1:17]]
            entry = w
            rd = {}
        elif t[0] == 'span' and entry is not None:
            x, y, n = int(t[2]), int(t[3]), int(t[4])
            vals = [int(v, 16) for v in t[5:5 + n]]
            if t[1] == 'rd':
                rd[(x, y)] = vals
            elif t[1] == 'wr' and (x, y) in rd and entry[2] == entry[3]:
                for d, o in zip(rd[(x, y)], vals):
                    if d != o:
                        rows.append((entry[2], d, o))
    print(len(rows), 'changed pixels with a one-colour entry')
    ks = [[], [], []]
    ratios = [[], []]
    for c, d, o in rows:
        cc, dd, oo = chans(c), chans(d), chans(o)
        if any(abs(cc[i] - dd[i]) <= 24 for i in range(3)):
            continue
        k = [(oo[i] - dd[i]) / (cc[i] - dd[i]) for i in range(3)]
        if k[0] < 0.05:
            continue
        for i in range(3):
            ks[i].append(k[i])
        ratios[0].append(k[1] / k[0])
        ratios[1].append(k[2] / k[0])
    if not ks[0]:
        print('no usable pixels')
        return
    m = lambda v: sum(v) / len(v)
    print(f'{len(ks[0])} pixels: mean k  Y {m(ks[0]):.3f}  Cr {m(ks[1]):.3f}  Cb {m(ks[2]):.3f}')
    print(f'mean k_Cr / k_Y {m(ratios[0]):.3f}   k_Cb / k_Y {m(ratios[1]):.3f}')
    srt = sorted(ratios[0])
    print('k_Cr / k_Y quartiles', [round(srt[int(len(srt) * q)], 3) for q in (0.1, 0.25, 0.5, 0.75, 0.9)])
    ex = rows[:: max(1, len(rows) // 8)][:8]
    for c, d, o in ex:
        print(f'  colour {chans(c)} dest {chans(d)} -> {chans(o)}')


if __name__ == '__main__':
    main()

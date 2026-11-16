"""Compare the T3K claw's response across levels from t3k --clawlog files (owner 2026-11-16: speed differs by level).

python -I tools/t3k_clawlevels.py logs/t3k/claw/L1.txt [more...]
clawlog columns: field, stick x, position (lanes), velocity word, scale, picture counter, d-pad input word, flags.
Per file: lanes covered a second while the stick is held (wraps unrolled by the lane count seen), and per picture the
game's actual move against what the driver assumed (scale x |input| x 768 / 65536): their ratio's median.
"""
import statistics
import sys


def main():
    for path in sys.argv[1:]:
        rows = [l.split() for l in open(path) if l.strip()]
        rows = [(int(r[0]), float(r[1]), float(r[2]), float(r[3]), float(r[4]), int(r[5]), int(r[6]), r[7]) for r in rows]
        top = max(r[2] for r in rows)
        # pictures: position at each new picture, with the input / scale set the field before
        pics = []
        for i in range(1, len(rows)):
            if rows[i][5] != rows[i - 1][5]:
                pics.append(i)
        ratios, moves = [], []
        for a, b in zip(pics, pics[1:]):
            d = rows[b][2] - rows[a][2]
            if abs(d) > 3:
                continue   # a wrap
            inp, sc = rows[b - 1][6], rows[b - 1][4]
            want = sc * abs(inp) * 768 / 65536
            if rows[a][1] > 0.9 and want > 0.05 and d != 0:
                ratios.append(abs(d) / want)
            if rows[a][1] > 0.9:
                moves.append(abs(d))
        # lanes a second while held right (fields with stick 1), unrolled
        held = [r for r in rows if r[1] > 0.9]
        dist = 0.0
        for x, y in zip(held, held[1:]):
            d = y[2] - x[2]
            if abs(d) > 3:
                d = 0   # (wrap: skip the field)
            dist += abs(d)
        secs = len(held) / 60.0
        print('%-22s max pos %5.2f  held %.1f s: %5.1f lanes/s  median move %.3f lanes/picture  actual/assumed %s' % (
            path, top, secs, dist / secs if secs else 0, statistics.median(moves) if moves else 0,
            '%.3f (n %d)' % (statistics.median(ratios), len(ratios)) if ratios else '-'))


if __name__ == '__main__':
    main()

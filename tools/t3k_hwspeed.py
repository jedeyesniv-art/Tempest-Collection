"""Estimate Tempest 3000's picture rate on real Nuon hardware from the emulator's instruction counts.

python -I tools/t3k_hwspeed.py <perflog.csv> [...]

The game moves per picture, so its designed speed is the picture rate the hardware reached. Per picture (the fields
between two flips) the packets each MPE executed are summed; at one packet a cycle and 54 MHz a picture needs at least
max over the MPEs (they run in parallel) / 54e6 seconds; flips land on 60 Hz vblanks, so it is shown for
ceil(that / one field) fields. A floor: memory / DMA stalls are not counted (Nuance's DMA completes at once, its wait
loops exit immediately); parked wait loops and stalled fields are not counted either.
"""
import csv
import math
import sys

CLOCK = 54e6
FIELD = CLOCK / 60.0   # cycles a field


def pictures(path):
    rows = list(csv.DictReader(open(path)))
    out = []
    acc = [0, 0, 0, 0]
    last = None
    for r in rows:
        fl = int(r['vid_flips'])
        for i in range(4):
            acc[i] += int(r['mpe%d' % i])
        if last is not None and fl != last:
            out.append((fl, list(acc)))
            acc = [0, 0, 0, 0]
        last = fl
    return out[2:]   # (the first pictures after a state load start mid-way)


def main():
    for path in sys.argv[1:]:
        pics = pictures(path)
        if not pics:
            continue
        fields = []
        crit = []
        for fl, a in pics:
            m = max(a)
            crit.append(a.index(m))
            fields.append(max(1, math.ceil(m / FIELD)))
        n = len(pics)
        avg = [sum(a[i] for _, a in pics) / n for i in range(4)]
        hist = {}
        for f in fields:
            hist[f] = hist.get(f, 0) + 1
        rate = 60.0 * n / sum(fields)
        print('%s: %d pictures; packets a picture MPE0 %.0fk MPE1 %.0fk MPE2 %.0fk MPE3 %.0fk; busiest MPE %s' % (
            path.split('/')[-1], n, avg[0] / 1e3, avg[1] / 1e3, avg[2] / 1e3, avg[3] / 1e3,
            max(set(crit), key=crit.count)))
        print('   fields a picture at 54 MHz: %s  ->  %.1f pictures/s (one field = %.0fk cycles)' % (
            ' '.join('%d:%d' % (k, v) for k, v in sorted(hist.items())), rate, FIELD / 1e3))


if __name__ == '__main__':
    main()

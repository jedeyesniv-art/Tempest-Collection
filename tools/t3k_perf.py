"""Summarise a t3k --perflog CSV by frame range: per video field, the MPE work (main-loop iterations) it got and
its wall-clock length.

python -I tools/t3k_perf.py log.csv [from:to ...]
"""
import csv
import statistics as st
import sys


def fields(rows):
    """Group retro_runs into fields (a field ends on a run with field_fired = 1)."""
    out, it, ms, pk = [], 0, 0.0, [0, 0, 0, 0]
    for r in rows:
        it += int(r['iterations'])
        ms += float(r['frame_ms'])
        if 'mpe0' in r:
            pk = [p + int(r['mpe%d' % i]) for i, p in enumerate(pk)]
        if r['field_fired'] == '1':
            out.append((it, ms, pk))
            it, ms, pk = 0, 0.0, [0, 0, 0, 0]
    return out


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p))]


def main():
    rows = list(csv.DictReader(open(sys.argv[1])))
    ranges = sys.argv[2:] or ['0:%d' % len(rows)]
    for rg in ranges:
        a, b = (int(x) for x in rg.split(':'))
        sel = [r for r in rows if a <= int(r['frame']) < b]
        f = fields(sel)
        if len(f) < 3:
            print(rg, 'too few fields')
            continue
        its = [x[0] for x in f[1:]]
        ms = [x[1] for x in f[1:]]
        print('%-11s %4d fields  iterations/field mean %6.0fk  p5 %6.0fk  p95 %6.0fk  min %6.0fk  max %6.0fk  '
              'cv %.3f | field ms mean %.2f p95 %.2f max %.2f | stalled %d' % (
                  rg, len(f) - 1, st.mean(its) / 1e3, pct(its, .05) / 1e3, pct(its, .95) / 1e3, min(its) / 1e3,
                  max(its) / 1e3, st.pstdev(its) / st.mean(its), st.mean(ms), pct(ms, .95), max(ms),
                  sum(int(r['mpe3_stalled']) for r in sel)))
        if 'vid_flips' in sel[0]:
            # The game's frame rate: flips (new pictures) per field, and how many fields each picture stayed up.
            flips = int(sel[-1]['vid_flips']) - int(sel[0]['vid_flips'])
            fl = [(int(r['field_counter']), int(r['vid_flips'])) for r in sel]
            holds, last_flip, last_field = [], fl[0][1], fl[0][0]
            for fc, vf in fl:
                if vf != last_flip:
                    holds.append(fc - last_field)
                    last_flip, last_field = vf, fc
            from collections import Counter
            print('    game: %.1f pictures/s (%.2f a field); fields per picture %s' % (
                flips * 60.0 / max(1, len(f) - 1), flips / max(1, len(f) - 1),
                dict(sorted(Counter(holds).items()))))
        if 'mpe0' in sel[0]:
            for i in range(4):
                v = [x[2][i] for x in f[1:]]
                print('    mpe%d packets/field mean %7.0fk  p5 %7.0fk  p95 %7.0fk  cv %.3f  (%.1f MHz at 60 fields/s)' % (
                    i, st.mean(v) / 1e3, pct(v, .05) / 1e3, pct(v, .95) / 1e3,
                    st.pstdev(v) / max(1, st.mean(v)), st.mean(v) * 60 / 1e6))


if __name__ == '__main__':
    main()

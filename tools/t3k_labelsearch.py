"""Rank Tempest 3000 memory words by how well their value predicts a label, from t3k --memdump dumps.

python -I tools/t3k_labelsearch.py <dir> <region> <label fields> [--skip <fields>]
Every dump m<F>_<region>.bin in the directory is used; label = 1 for fields in the ranges (F0-F1,...), dumps in
--skip ranges are ignored (ambiguous edges). Words with 2..12 distinct values: each value votes its majority label;
prints the most accurate words with their value -> label table.
"""
import glob
import os
import re
import sys

import numpy as np

BASE = {'sys': 0x80000000, 'main': 0x40000000, 'mpe3': 0x20000000}


def ranges(spec):
    out = []
    for part in spec.split(','):
        a, b = (int(x) for x in part.split('-'))
        out.append((a, b))
    return out


def main():
    d, region = sys.argv[1], sys.argv[2]
    lab = ranges(sys.argv[3])
    skip = ranges(sys.argv[sys.argv.index('--skip') + 1]) if '--skip' in sys.argv else []
    fs = sorted(int(re.search(r'm(\d+)_', os.path.basename(p)).group(1))
                for p in glob.glob(os.path.join(d, 'm*_%s.bin' % region)))
    fs = [f for f in fs if not any(a <= f <= b for a, b in skip)]
    y = np.array([any(a <= f <= b for a, b in lab) for f in fs])
    V = np.stack([np.fromfile(os.path.join(d, 'm%d_%s.bin' % (f, region)), dtype='>u4') for f in fs])
    res = []
    for w in range(V.shape[1]):
        col = V[:, w]
        vals = np.unique(col)
        if not 2 <= len(vals) <= 12:
            continue
        ok = 0
        table = []
        for v in vals:
            m = col == v
            k = int(y[m].sum())
            maj = k * 2 >= m.sum()
            ok += k if maj else int(m.sum()) - k
            table.append((v, int(m.sum()), k))
        res.append((ok / len(fs), w, table))
    res.sort(key=lambda r: -r[0])
    print('%d dumps, %d labelled' % (len(fs), int(y.sum())))
    for acc, w, table in res[:25]:
        print('%08X %.3f  %s' % (BASE[region] + 4 * w, acc,
                                 '  '.join('%X:%d/%d' % (v, k, n) for v, n, k in table)))


if __name__ == '__main__':
    main()

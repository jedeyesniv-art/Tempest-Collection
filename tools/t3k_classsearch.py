"""Find Tempest 3000 variables that tell screens apart, from t3k --memdump dumps (m<field>_<region>.bin).

python -I tools/t3k_classsearch.py <dir> <region: sys|mpe3> <in fields> -- <out fields>
fields: comma-separated ranges F0-F1 (inclusive, dumps that exist). Prints the words (big-endian u32) whose values
over the "in" dumps never occur in the "out" dumps, fewest distinct "in" values first.
"""
import os
import sys

import numpy as np

BASE = {'sys': 0x80000000, 'main': 0x40000000, 'mpe3': 0x20000000}


def frames(d, region, spec):
    out = []
    for part in spec.split(','):
        a, b = (int(x) for x in part.split('-'))
        out += [f for f in range(a, b + 1) if os.path.exists(os.path.join(d, 'm%d_%s.bin' % (f, region)))]
    return out


def load(d, region, fs):
    return np.stack([np.fromfile(os.path.join(d, 'm%d_%s.bin' % (f, region)), dtype='>u4') for f in fs])


def main():
    d, region = sys.argv[1], sys.argv[2]
    sep = sys.argv.index('--')
    fin, fout = frames(d, region, sys.argv[3]), frames(d, region, sys.argv[sep + 1])
    A, B = load(d, region, fin), load(d, region, fout)
    # candidates: few distinct values inside (<= 4), none of them outside
    amin, amax = A.min(0), A.max(0)
    hits = []
    for w in range(A.shape[1]):
        a = np.unique(A[:, w])
        if len(a) > int(os.environ.get("MAXV", "4")):
            continue
        if np.isin(B[:, w], a).any():
            continue
        hits.append((len(a), w, a, np.unique(B[:, w])))
    hits.sort(key=lambda h: (h[0], len(h[3])))
    print('%d in dumps, %d out dumps, %d words match' % (len(fin), len(fout), len(hits)))
    for n, w, a, b in hits[:60]:
        print('%08X in %s  out %d values: %s' % (BASE[region] + 4 * w, ' '.join('%08X' % v for v in a), len(b),
                                                 ' '.join('%08X' % v for v in b[:6])))


if __name__ == '__main__':
    main()

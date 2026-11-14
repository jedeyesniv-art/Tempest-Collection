"""Search t3k --memdump dumps for game variables.

python -I tools/t3k_memsearch.py <dir> <region: sys|main|mpe3> F0 F1 ... -- <rule>
rules (over the dumps' consecutive deltas, as big-endian signed 32-bit words):
  claw:R0:R1:L0:L1   rises over fields R0..R1 (right held), falls over L0..L1 (left held), still elsewhere
  count:N1,N2,...    deltas equal exactly the given numbers (e.g. flips per interval)
Prints matching addresses (region base added) with their values.
"""
import os
import sys

import numpy as np

BASE = {'sys': 0x80000000, 'main': 0x40000000, 'mpe3': 0x20000000}


def main():
    d, region = sys.argv[1], sys.argv[2]
    sep = sys.argv.index('--')
    frames = [int(x) for x in sys.argv[3:sep]]
    rule = sys.argv[sep + 1]
    dumps = []
    for f in frames:
        raw = np.fromfile(os.path.join(d, 'm%d_%s.bin' % (f, region)), dtype='>i4').astype(np.int64)
        dumps.append(raw)
    V = np.stack(dumps)          # frames x words
    D = np.diff(V, axis=0)       # deltas between consecutive dumps
    if rule.startswith('claw:'):
        r0, r1, l0, l1 = (int(x) for x in rule[5:].split(':'))
        mask = np.ones(V.shape[1], bool)
        for k in range(len(frames) - 1):
            a, b = frames[k], frames[k + 1]
            if a >= r0 and b <= r1:
                mask &= D[k] > 0
            elif a >= l0 and b <= l1:
                mask &= D[k] < 0
        mask &= (V.max(0) - V.min(0)) < 1 << 24
    elif rule.startswith('count:'):
        want = [int(x) for x in rule[6:].split(',')]
        assert len(want) == len(frames) - 1
        mask = np.ones(V.shape[1], bool)
        for k, w in enumerate(want):
            mask &= D[k] == w
    else:
        raise SystemExit('unknown rule')
    idx = np.nonzero(mask)[0]
    print('%d matches' % len(idx))
    for i in idx[:40]:
        print('  %08X  %s' % (BASE[region] + 4 * i, ' '.join('%d' % v for v in V[:, i])))


if __name__ == '__main__':
    main()

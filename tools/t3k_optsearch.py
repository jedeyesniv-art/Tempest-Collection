"""Find Tempest 3000 memory words that follow a setting, from t3k --memdump dumps.

python -I tools/t3k_optsearch.py <dir> <region: sys|main> F:label F:label ...
Each dump m<F>_<region>.bin gets a label (any string); prints the words whose value is the same for equal labels and
differs for different labels (byte granularity, so flags in a byte show up too).
"""
import os
import sys

import numpy as np

BASE = {'sys': 0x80000000, 'main': 0x40000000, 'mpe3': 0x20000000, 'mpe0': 0x20000000, 'mpe1': 0x20000000, 'mpe2': 0x20000000}


def main():
    d, region = sys.argv[1], sys.argv[2]
    pairs = [a.split(':') for a in sys.argv[3:]]
    V = np.stack([np.fromfile(os.path.join(d, 'm%s_%s.bin' % (f, region)), dtype=np.uint8) for f, _ in pairs])
    labels = [l for _, l in pairs]
    mask = np.ones(V.shape[1], bool)
    for i in range(len(labels)):
        for j in range(i + 1, len(labels)):
            if labels[i] == labels[j]:
                mask &= V[i] == V[j]
            else:
                mask &= V[i] != V[j]
    hits = np.nonzero(mask)[0]
    print('%d bytes' % len(hits))
    for a in hits[:60]:
        print('$%08X  %s' % (BASE[region] + a, ' '.join('%02X' % V[k, a] for k in range(len(labels)))))


if __name__ == '__main__':
    main()

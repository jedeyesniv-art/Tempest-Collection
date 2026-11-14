"""Find the claw's state: words that hold still while idle, move one way while right is held and the other way while
left is held. Needs a --memdump every field over [F0, F1] (T3K_MEMDUMP_REGIONS=sys,mpe3).

python -I tools/t3k_clawsearch.py <dir> F0 F1 R0 R1 L0 L1 [settle]
(R0..R1 right held, L0..L1 left held; idle = everything else, ignoring `settle` fields after each release.)
"""
import os
import sys

import numpy as np

BASE = {'sys': 0x80000000, 'mpe3': 0x20000000}


def load(d, region, frames, dtype):
    return np.stack([np.fromfile(os.path.join(d, 'm%d_%s.bin' % (f, region)), dtype=dtype).astype(np.int64)
                     for f in frames])


def main():
    d = sys.argv[1]
    f0, f1, r0, r1, l0, l1 = (int(x) for x in sys.argv[2:8])
    settle = int(sys.argv[8]) if len(sys.argv) > 8 else 6
    frames = list(range(f0, f1 + 1))
    idx = {f: i for i, f in enumerate(frames)}
    idle = [f for f in frames if not (r0 <= f < r1 + settle) and not (l0 <= f < l1 + settle)]
    for region in ('sys', 'mpe3'):
        for dtype, size in (('>i4', 4), ('>i2', 2)):
            V = load(d, region, frames, dtype)
            if region == 'mpe3':
                V = V[:, :0x200000 // size]   # local RAM is small; the dump is 8 MB of mostly nothing
            ok = np.ones(V.shape[1], bool)
            # still while idle (each idle run constant)
            runs, cur = [], []
            for f in idle:
                if cur and f != cur[-1] + 1:
                    runs.append(cur)
                    cur = []
                cur.append(f)
            runs.append(cur)
            for run in runs:
                a = V[[idx[f] for f in run]]
                ok &= (a == a[0]).all(axis=0)
            dr = V[idx[min(r1 + settle, f1)]] - V[idx[r0]]
            dl = V[idx[min(l1 + settle, f1)]] - V[idx[l0]]
            ok &= ((dr > 0) & (dl < 0)) | ((dr < 0) & (dl > 0))
            hits = np.nonzero(ok)[0]
            print('%s %s: %d candidates' % (region, dtype, len(hits)))
            for i in hits[:25]:
                vals = V[:, i]
                show = ' '.join(str(vals[idx[f]]) for f in range(f0, f1 + 1, 5))
                print('  %08X  %s' % (BASE[region] + size * i, show))


if __name__ == '__main__':
    main()

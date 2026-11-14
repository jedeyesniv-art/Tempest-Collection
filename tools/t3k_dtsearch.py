"""With a --memdump every field (and the --perflog of that run), look for how the game times itself:
- the picture counter (given address) and the field counter;
- words that change only when a picture is drawn and then hold (fields since the last picture = a delta time),
  i.e. at each new picture the value equals the number of fields that picture took (or a fixed multiple).
- the claw position word over time vs pictures.

python -I tools/t3k_dtsearch.py <dir> F0 F1 picture_counter_addr [claw_addr]
"""
import os
import sys

import numpy as np


def main():
    d, f0, f1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    pic_addr = int(sys.argv[4], 16)
    claw_addr = int(sys.argv[5], 16) if len(sys.argv) > 5 else None
    frames = list(range(f0, f1 + 1))
    V = np.stack([np.fromfile(os.path.join(d, 'm%d_sys.bin' % f), dtype='>i4').astype(np.int64) for f in frames])
    pic = V[:, (pic_addr - 0x80000000) // 4]
    print('picture counter:', ' '.join(str(x) for x in pic))
    # Fields each picture took: for every field where the picture counter advanced, the fields since the last advance.
    new = [k for k in range(1, len(frames)) if pic[k] != pic[k - 1]]
    took = [b - a for a, b in zip(new, new[1:])]
    print('fields per picture:', took)
    # Candidates: words whose value, at each picture after the first, is proportional to `took` (constant ratio != 0),
    # and which only change at pictures.
    cand = []
    at = new[1:]
    took = np.array(took)
    Vat = V[at, :]
    for scale in (1, 2, 4, 8, 16, 0x10000, 0x8000):
        m = np.all(Vat == took[:, None] * scale, axis=0)
        for i in np.nonzero(m)[0]:
            cand.append((scale, i))
    # Also the "same picture took k fields" one-off lag: value at a picture = fields the previous picture took.
    lag = np.all(Vat[1:] == took[:-1, None], axis=0)
    print('%d proportional candidates, %d lagged' % (len(cand), int(lag.sum())))
    for scale, i in cand[:30]:
        print('  %08X x%d: %s' % (0x80000000 + 4 * i, scale, ' '.join(str(x) for x in V[:, i])))
    for i in np.nonzero(lag)[0][:10]:
        print('  lagged %08X: %s' % (0x80000000 + 4 * i, ' '.join(str(x) for x in V[:, i])))
    if claw_addr:
        c = V[:, (claw_addr - 0x80000000) // 4] / 65536.0
        print('claw:', ' '.join('%.3f' % x for x in c))
        steps = [c[b] - c[a] for a, b in zip(new, new[1:])]
        print('claw step per picture:', ' '.join('%.3f' % s for s in steps))
        print('claw step per field of that picture:', ' '.join('%.3f' % (s / t) for s, t in zip(steps, took)))


if __name__ == '__main__':
    main()

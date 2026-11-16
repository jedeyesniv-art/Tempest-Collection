"""Per-frame values of the changing words in a window of per-frame range dumps (txk.exe --memseq).

  python tools/txk_memtrace.py <seq dir> <base hex> <addr hex> <before> <after> [--every N] [--int]
Columns = words in [addr - before, addr + after) that change during the sequence; rows = frames.
"""
import glob
import os
import sys

import numpy as np


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    every = next((int(a.split('=')[1]) for a in sys.argv if a.startswith('--every=')), 1)
    as_int = '--int' in sys.argv
    d, base, addr, before, after = args[0], int(args[1], 16), int(args[2], 16), int(args[3]), int(args[4])
    files = sorted(glob.glob(os.path.join(d, 'f*.bin')))
    lo = (addr - before - base) // 4
    hi = (addr + after - base) // 4
    rows = []
    for f in files:
        w = np.fromfile(f, dtype=np.uint32)[lo:hi]
        rows.append(w)
    m = np.stack(rows)
    cols = [j for j in range(m.shape[1]) if len(np.unique(m[:, j])) > 1]
    print('frame  ' + ' '.join(f'{base + 4 * (lo + j):>9X}' for j in cols))
    for i, f in enumerate(files):
        if i % every:
            continue
        vals = []
        for j in cols:
            w = int(m[i, j])
            if as_int:
                vals.append(f'{w - (1 << 32) if w >= 1 << 31 else w:>9d}')
            else:
                v = np.array([w], dtype=np.uint32).view(np.float32)[0]
                vals.append(f'{v:>9.4g}')
        print(f'{os.path.basename(f)[1:6]}  ' + ' '.join(vals))


if __name__ == '__main__':
    main()

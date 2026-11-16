"""Words in per-frame range dumps (txk.exe --memseq) that are still while the claw rests and change once it moves.

  python tools/txk_memmove.py <seq dir> <base hex> <rest f0> <rest f1> <move f0> <move f1> [--show]
Prints address and float trace (every frame from rest f0 to the end with --show, else every 4th).
"""
import glob
import os
import sys

import numpy as np


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    d, base = args[0], int(args[1], 16)
    r0, r1, m0, m1 = map(int, args[2:6])
    files = sorted(glob.glob(os.path.join(d, 'f*.bin')))
    frames = [int(os.path.basename(f)[1:6]) for f in files]
    data = np.stack([np.fromfile(f, dtype=np.uint32) for f in files])
    fr = np.array(frames)
    rest = data[(fr >= r0) & (fr <= r1)]
    move = data[(fr >= m0) & (fr <= m1)]
    still = np.all(rest == rest[0], axis=0)
    moved = np.any(move != rest[0], axis=0)
    cand = np.nonzero(still & moved)[0]
    print(f'{len(cand)} words')
    fl = data.view(np.float32)
    step = 1 if '--show' in sys.argv else 4
    for j in cand[:200]:
        tr = ' '.join(f'{v:.4g}' for v in fl[::step, j][: 40])
        print(f'{base + 4 * j:08X} | {tr}')


if __name__ == '__main__':
    main()

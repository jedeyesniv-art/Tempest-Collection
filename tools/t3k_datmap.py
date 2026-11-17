"""Map Tempest 3000's nuon.dat: which stretches look like 16-bit BE stereo PCM, which don't.

Per 2048-byte sector: smoothness = mean |s[n] - s[n-1]| / mean |s[n]| per channel (audio is smooth, < ~0.5;
packed data / code is ~1.4 or more), plus zero / constant sectors. Runs of the same class are merged.
Usage: t3k_datmap.py <iso> [--sector-lba 275] [--size 736636928] [--min 8]
"""
import argparse
import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument('iso')
ap.add_argument('--lba', type=int, default=275)
ap.add_argument('--size', type=int, default=736636928)
ap.add_argument('--min', type=int, default=8, help='merge runs shorter than this (sectors) into neighbours')
a = ap.parse_args()

raw = np.memmap(a.iso, dtype='>i2', mode='r', offset=a.lba * 2048, shape=(a.size // 2,))
n = a.size // 2048
s = np.asarray(raw[:n * 1024], dtype=np.float32).reshape(n, 512, 2)
d = np.abs(np.diff(s, axis=1)).mean(axis=(1, 2))
m = np.abs(s).mean(axis=(1, 2))
cls = np.where(m == 0, 'Z', np.where(d / np.maximum(m, 1) < 0.6, 'A', 'D'))
# a second check for "data": byte histogram entropy per sector
b = np.frombuffer(np.asarray(raw[:n * 1024]).tobytes(), dtype=np.uint8).reshape(n, 2048)

runs = []
start = 0
for i in range(1, n + 1):
    if i == n or cls[i] != cls[start]:
        runs.append([cls[start], start, i - start])
        start = i
# merge short runs
merged = []
for r in runs:
    if merged and (r[2] < a.min or merged[-1][0] == r[0]):
        merged[-1][2] += r[2]
        if merged[-1][0] != r[0] and r[2] >= a.min:
            pass
    else:
        merged.append(r)
tot = {'A': 0, 'D': 0, 'Z': 0}
for c, st, ln in merged:
    tot[c] += ln
    sec = ln * 2048 / (44100 * 4)
    print(f'{c} sector {st:7d} off {st * 2048:#011x} len {ln:7d} sectors {ln * 2048 / 1e6:8.2f} MB  ~{sec:7.1f} s @44.1k')
print({k: f'{v * 2048 / 1e6:.1f} MB' for k, v in tot.items()})

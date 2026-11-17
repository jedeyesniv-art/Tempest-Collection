"""Compare two recordings of the same run (e.g. t3k from the .iso and from the .nuz): per-second SNR of b against a,
after the best small alignment. python -I tools/wav_compare.py a.wav b.wav [max lag samples]"""
import sys
import wave
import numpy as np


def load(p):
    w = wave.open(p)
    return w.getframerate(), np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64).reshape(-1, w.getnchannels())


ra, a = load(sys.argv[1])
rb, b = load(sys.argv[2])
assert ra == rb
n = min(len(a), len(b))
a, b = a[:n], b[:n]
lagmax = int(sys.argv[3]) if len(sys.argv) > 3 else 64
best = min(range(-lagmax, lagmax + 1), key=lambda k: np.abs(a[max(0, k):n + min(0, k)] - b[max(0, -k):n - max(0, k)]).mean())
print(f'{n / ra:.1f} s, best lag {best} samples, identical samples: {np.mean(np.all(a == b, axis=1)) * 100:.1f}%')
a, b = a[max(0, best):n + min(0, best)], b[max(0, -best):n - max(0, best)]
sec = ra
line = []
for i in range(0, len(a) - sec, sec):
    x, y = a[i:i + sec], b[i:i + sec]
    s, e = (x * x).sum(), ((x - y) ** 2).sum()
    line.append('  -  ' if s < 1e3 else ('exact' if e == 0 else f'{10 * np.log10(s / e):5.1f}'))
for i in range(0, len(line), 10):
    print(f'{i:4d}s ' + ' '.join(line[i:i + 10]))

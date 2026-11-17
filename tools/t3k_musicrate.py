"""Tempest 3000 music: find the sample rate of nuon.dat's PCM by matching a track against a recording of the game
(t3k --headless --wav). For each candidate rate the raw track is resampled to the recording's rate and the best
normalised cross-correlation (over all lags) is printed; the true rate stands out.

python -I tools/t3k_musicrate.py <iso> <recording.wav> <track start block> [rec from s] [rec len s]
"""
import sys
import wave
import numpy as np

iso, wavp, blk = sys.argv[1], sys.argv[2], int(sys.argv[3])
t0 = float(sys.argv[4]) if len(sys.argv) > 4 else 10.0
tl = float(sys.argv[5]) if len(sys.argv) > 5 else 4.0
DAT_LBA = 275

w = wave.open(wavp)
rate = w.getframerate()
ch = w.getnchannels()
rec = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64).reshape(-1, ch).mean(axis=1)
seg = rec[int(t0 * rate):int((t0 + tl) * rate)]
seg = (seg - seg.mean()) / (seg.std() + 1e-9)

f = open(iso, 'rb')
f.seek((DAT_LBA + blk) * 2048)
raw = np.frombuffer(f.read(30 * 48000 * 4), dtype='>i2').astype(np.float64).reshape(-1, 2).mean(axis=1)

for r in (22050, 24000, 32000, 44100, 48000):
    n_out = int(len(raw) * rate / r)
    x = np.interp(np.arange(n_out) * (r / rate), np.arange(len(raw)), raw)
    # sliding normalised correlation of seg against x
    n = len(seg)
    L = 1 << int(np.ceil(np.log2(len(x) + n)))
    c = np.fft.irfft(np.fft.rfft(x, L) * np.conj(np.fft.rfft(seg, L)), L)[:len(x) - n]
    cs = np.concatenate([[0], np.cumsum(x)])
    cs2 = np.concatenate([[0], np.cumsum(x * x)])
    m = (cs[n:n + len(c)] - cs[:len(c)]) / n
    v = (cs2[n:n + len(c)] - cs2[:len(c)]) / n - m * m
    nc = c / (n * np.sqrt(np.maximum(v, 1e-9)))
    k = int(np.argmax(nc))
    print(f'{r:6d} Hz: best correlation {nc[k]:.3f} at track time {k / rate:.2f} s')

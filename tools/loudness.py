"""Loudness of headless --wav recordings, ITU-R BS.1770-4 (what LUFS meters show): K-weighting (a high shelf +4 dB
above ~1.5 kHz and a 38 Hz high-pass, designed for the file's own sample rate), 400 ms blocks every 100 ms, the
absolute gate at -70 LUFS and the relative gate 10 LU under the ungated mean. Stereo: both channels weight 1.

python -I tools/loudness.py file.wav[:t0-t1] ... [--target -16] [--frames]

  :t0-t1     a part of the file, in seconds (or in 60 Hz video frames with --frames), e.g. t2k.wav:40-110
  --target   prints the gain (dB and linear) that brings each one to this loudness

Prints integrated loudness (LUFS), loudness range of the blocks (10th..95th percentile, LU) and the sample peak (dBFS).
"""
import sys
import wave

import numpy as np
from scipy.signal import lfilter


def read(path):
    with wave.open(path, 'rb') as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        assert width == 2, 'want 16-bit PCM'
        x = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float64) / 32768.0
    return x.reshape(-1, ch), rate


def biquad_k(rate):
    """The two K-weighting stages for any rate (the BS.1770 analogue prototypes through the bilinear transform, as
    pyloudnorm / libebur128 do)."""
    # stage 1: high shelf
    f0, g, q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    k = np.tan(np.pi * f0 / rate)
    vh = 10 ** (g / 20)
    vb = vh ** 0.4996667741545416
    a0 = 1 + k / q + k * k
    b1 = np.array([vh + vb * k / q + k * k, 2 * (k * k - vh), vh - vb * k / q + k * k]) / a0
    a1 = np.array([1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0])
    # stage 2: high-pass
    f0, q = 38.13547087602444, 0.5003270373238773
    k = np.tan(np.pi * f0 / rate)
    a0 = 1 + k / q + k * k
    b2 = np.array([1, -2, 1])
    a2 = np.array([1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0])
    return (b1, a1), (b2, a2)


def measure(x, rate):
    (b1, a1), (b2, a2) = biquad_k(rate)
    y = lfilter(b2, a2, lfilter(b1, a1, x, axis=0), axis=0)
    block, step = int(0.4 * rate), int(0.1 * rate)
    if len(y) < block:
        return float('-inf'), 0.0
    sq = (y ** 2).sum(axis=1)   # channel weights 1
    c = np.concatenate([[0.0], np.cumsum(sq)])
    starts = np.arange(0, len(y) - block + 1, step)
    z = (c[starts + block] - c[starts]) / block
    with np.errstate(divide='ignore'):
        lk = -0.691 + 10 * np.log10(z)
    z1 = z[lk > -70]
    if len(z1) == 0:
        return float('-inf'), 0.0
    rel = -0.691 + 10 * np.log10(z1.mean()) - 10
    z2 = z[(lk > -70) & (lk > rel)]
    integrated = -0.691 + 10 * np.log10(z2.mean())
    l2 = -0.691 + 10 * np.log10(z2)
    lra = float(np.percentile(l2, 95) - np.percentile(l2, 10))
    return integrated, lra


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    target = None
    if '--target' in sys.argv:
        target = float(sys.argv[sys.argv.index('--target') + 1])
        args = [a for a in args if a != sys.argv[sys.argv.index('--target') + 1]]
    unit = 1 / 60 if '--frames' in sys.argv else 1.0
    for a in args:
        path, part = a, None
        if ':' in a[2:]:   # (a drive letter's colon is at 1)
            i = a.rindex(':')
            path, part = a[:i], a[i + 1:]
        x, rate = read(path)
        if part:
            t0, t1 = (float(v) * unit for v in part.split('-'))
            x = x[int(t0 * rate):int(t1 * rate)]
        lufs, lra = measure(x, rate)
        peak = 20 * np.log10(max(np.abs(x).max(), 1e-9))
        line = f'{a:40s} {lufs:7.2f} LUFS  LRA {lra:5.1f} LU  peak {peak:6.1f} dBFS  {len(x) / rate:6.1f} s @ {rate} Hz'
        if target is not None:
            g = target - lufs
            line += f'  gain {g:+6.2f} dB = x{10 ** (g / 20):.3f}'
        print(line)


if __name__ == '__main__':
    main()

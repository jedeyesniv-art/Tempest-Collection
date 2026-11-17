"""Where sounds start in a recording (headless --wav of our game programs): per video frame (1/60 s) the loudness, and
the frames where it jumps up (an onset). For finding a game's sound effects by lining them up with scripted input.

python -I tools/wav_events.py rec.wav [--fps 60] [--from F0] [--to F1] [--env]

  --env   print the loudness of every frame in range (dBFS), not only the onsets
"""
import argparse
import math
import struct
import wave


def frames_rms(path, fps):
    w = wave.open(path)
    ch, rate, sw = w.getnchannels(), w.getframerate(), w.getsampwidth()
    assert sw == 2, '16-bit only'
    data = w.readframes(w.getnframes())
    n = len(data) // (2 * ch)
    samples = struct.unpack('<%dh' % (n * ch), data)
    per = rate / fps
    out = []
    f = 0
    while int((f + 1) * per) <= n:
        a, b = int(f * per), int((f + 1) * per)
        s = 0.0
        for i in range(a, b):
            for c in range(ch):
                v = samples[i * ch + c]
                s += v * v
        rms = math.sqrt(s / ((b - a) * ch)) / 32768.0
        out.append(20 * math.log10(rms) if rms > 1e-6 else -120.0)
        f += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wav')
    ap.add_argument('--fps', type=float, default=60.0)
    ap.add_argument('--from', dest='f0', type=int, default=0)
    ap.add_argument('--to', dest='f1', type=int, default=10 ** 9)
    ap.add_argument('--env', action='store_true')
    a = ap.parse_args()
    db = frames_rms(a.wav, a.fps)
    f1 = min(a.f1, len(db) - 1)
    for f in range(a.f0, f1 + 1):
        if a.env:
            print('%6d %7.1f %s' % (f, db[f], '#' * max(0, int((db[f] + 70) / 2))))
        elif f > 0 and db[f] > -55 and db[f] - db[f - 1] > 9:
            print('onset frame %d: %.1f dB (from %.1f)' % (f, db[f], db[f - 1]))


if __name__ == '__main__':
    main()

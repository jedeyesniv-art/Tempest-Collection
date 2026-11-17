"""Tempest 2000's raw sound samples (reference/tempest2k/src/sounds/samples/NN, 8-bit): find the byte format and the
playback rate by matching a sample against a recording of the game (headless --wav) - the rate with the best
normalised cross-correlation wins.

python -I tools/t2k_samplerate.py <sample file> <recording.wav> <from s> <to s>
"""
import math
import struct
import sys
import wave


def load_sample(path, signed):
    d = open(path, 'rb').read()
    return [((b - 256) if b > 127 else b) / 128.0 for b in d] if signed else [(b - 128) / 128.0 for b in d]


def load_wav(path, t0, t1, rate_out):
    w = wave.open(path)
    ch, rate = w.getnchannels(), w.getframerate()
    w.setpos(int(t0 * rate))
    n = int((t1 - t0) * rate)
    s = struct.unpack('<%dh' % (n * ch), w.readframes(n))
    mono = [sum(s[i * ch:(i + 1) * ch]) / (ch * 32768.0) for i in range(n)]
    step = rate / rate_out   # decimate to rate_out (box average)
    out = []
    i = 0.0
    while int(i + step) <= len(mono):
        a, b = int(i), int(i + step)
        out.append(sum(mono[a:b]) / max(1, b - a))
        i += step
    return out


def resample(x, rate_in, rate_out):
    n = int(len(x) * rate_out / rate_in)
    out = []
    for k in range(n):
        t = k * rate_in / rate_out
        i = int(t)
        f = t - i
        a = x[i] if i < len(x) else 0.0
        b = x[i + 1] if i + 1 < len(x) else 0.0
        out.append(a * (1 - f) + b * f)
    return out


def best_corr(sample, rec):
    # envelope correlation (robust to filtering / phase): 10 ms RMS frames
    def env(x, n=40):
        return [math.sqrt(sum(v * v for v in x[i:i + n]) / n) for i in range(0, len(x) - n, n)]
    es, er = env(sample), env(rec)
    if len(es) >= len(er):
        return 0.0, 0
    ms = sum(es) / len(es)
    best = (-1.0, 0)
    for off in range(0, len(er) - len(es)):
        seg = er[off:off + len(es)]
        mr = sum(seg) / len(seg)
        num = sum((a - ms) * (b - mr) for a, b in zip(es, seg))
        den = math.sqrt(sum((a - ms) ** 2 for a in es) * sum((b - mr) ** 2 for b in seg)) or 1
        if num / den > best[0]:
            best = (num / den, off)
    return best


def main():
    path, rec_path, t0, t1 = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
    raw = open(path, 'rb').read()
    hist_mid = sum(1 for b in raw if 0x70 <= b <= 0x90)
    hist_zero = sum(1 for b in raw if b <= 0x10 or b >= 0xF0)
    print('%s: %d bytes; near 0x80: %d, near 0x00: %d -> %s' % (path, len(raw), hist_mid, hist_zero,
          'signed' if hist_zero > hist_mid else 'unsigned'))
    signed = hist_zero > hist_mid
    x = load_sample(path, signed)
    work = 4000.0
    rec = load_wav(rec_path, t0, t1, work)
    for rate in (5000, 6000, 6927, 8000, 8287, 9000, 10000, 11025, 12000, 13000, 14000, 16574, 18000, 22050):
        c, off = best_corr(resample(x, rate, work), rec)
        print('rate %6d Hz: %.1f s long, correlation %.3f at +%.2f s' % (rate, len(x) / rate, c, off * 0.01))


if __name__ == '__main__':
    main()

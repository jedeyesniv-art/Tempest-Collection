"""The launcher's sound stings (src/launcher_sound.cpp): for each game its lane-switch sound (played when the
selection moves to it) and its death / explosion (played when it is started), every one the game's own.

python -I tools/launcher_sounds.py            -> art\\sounds\\<id>_move.wav, <id>_launch.wav (not in git)
                                                 + art\\sounds\\sg_bank\\NN.wav (all 73 Space Giraffe sounds, to pick from)

Sources (found 2026-11-17):
  t81  recorded from t81.exe (MAME): the spinner's lane click (frames 1302-1309 after a move at 1300), the death
       (0.8 s from frame 1728; three identical ones in the run)
  t2k  the game's raw samples (reference/tempest2k/src/sounds/samples, signed 8-bit; played at ~16 kHz, matched
       against a recording): 'tink' (a flipper landing on a lane; the claw itself moves silently - yak.s domove has
       no sound), 'screeeam' (gotu: a flipper catching the claw)
  t3k  recorded from t3k.exe with the music level poked to 0 at boot: the claw's lane click (repeats per lane while
       held; frame 3310), the death (2.3 s from frame 3510, then the warp back in)
  sg   GRsounds.xwb (no names in the bank): SG_MOVE / SG_LAUNCH below (the beep, the phone - owner's choice)
  txk  the game's assets: tink.wav (the same sample T4K has), shipexplosion.wav (TxK's own)
  t4k  the Steam install's assets: clawmove.wav, shipexplosion.wav
"""
import math
import os
import struct
import subprocess
import sys
import wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'art', 'sounds')
REC = os.path.join(ROOT, 'logs', 'sounds')
STEAM = r'C:\Program Files (x86)\Steam\steamapps\common'
TXK = None   # found in main(): the play folder's or build's game\PCSB00494\assets
SG_MOVE, SG_LAUNCH = 21, 34   # GRsounds.xwb entries: the beep and the phone (owner: perfect, 2026-11-17)
RATE = 48000


# ---- reading ----
def read_wav(path, t0=0.0, t1=None):
    w = wave.open(path)
    ch, rate, sw = w.getnchannels(), w.getframerate(), w.getsampwidth()
    a = int(t0 * rate)
    b = w.getnframes() if t1 is None else min(w.getnframes(), int(t1 * rate))
    w.setpos(a)
    raw = w.readframes(b - a)
    if sw == 2:
        s = struct.unpack('<%dh' % (len(raw) // 2), raw)
        mono = [sum(s[i:i + ch]) / (ch * 32768.0) for i in range(0, len(s), ch)]
    elif sw == 1:
        mono = [sum(raw[i + c] - 128 for c in range(ch)) / (ch * 128.0) for i in range(0, len(raw), ch)]
    else:
        raise SystemExit('%s: %d-bit' % (path, sw * 8))
    return mono, rate


def t2k_sample(n, rate=16000):
    d = open(os.path.join(ROOT, 'reference', 'tempest2k', 'src', 'sounds', 'samples', '%02d' % n), 'rb').read()
    return [((b - 256) if b > 127 else b) / 128.0 for b in d], rate


def ms_adpcm(data, ch, block_align):
    coef = [(256, 0), (512, -256), (0, 0), (192, 64), (240, 0), (460, -208), (392, -232)]
    adapt = [230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230]
    out = []
    for at in range(0, len(data) - 7 * ch + 1, block_align):
        blk = data[at:at + block_align]
        if len(blk) < 7 * ch:
            break
        pred = list(blk[:ch])
        p = ch
        delta = [struct.unpack('<h', blk[p + 2 * c:p + 2 * c + 2])[0] for c in range(ch)]
        p += 2 * ch
        s1 = [struct.unpack('<h', blk[p + 2 * c:p + 2 * c + 2])[0] for c in range(ch)]
        p += 2 * ch
        s2 = [struct.unpack('<h', blk[p + 2 * c:p + 2 * c + 2])[0] for c in range(ch)]
        p += 2 * ch
        frames = [s2[:], s1[:]]
        nib = []
        for byte in blk[p:]:
            nib += [byte >> 4, byte & 15]
        c = 0
        cur = []
        for nb in nib:
            k1, k2 = coef[min(pred[c], 6)]
            pr = (s1[c] * k1 + s2[c] * k2) >> 8
            sn = nb - 16 if nb & 8 else nb
            v = max(-32768, min(32767, pr + sn * delta[c]))
            s2[c], s1[c] = s1[c], v
            delta[c] = max(16, (adapt[nb] * delta[c]) >> 8)
            cur.append(v)
            c += 1
            if c == ch:
                frames.append(cur)
                cur = []
                c = 0
        for f in frames:
            out.append(sum(f) / (ch * 32768.0))
    return out


def sg_bank():
    """Every sound of Space Giraffe's GRsounds.xwb as (mono samples, rate)."""
    p = os.path.join(STEAM, 'Space Giraffe', 'media', 'sounds', 'GRsounds.xwb')
    d = open(p, 'rb').read()
    regions = [struct.unpack('<II', d[12 + i * 8:20 + i * 8]) for i in range(5)]
    bd = regions[0][0]
    count = struct.unpack('<I', d[bd + 4:bd + 8])[0]
    metasz = struct.unpack('<I', d[bd + 72:bd + 76])[0]
    md, wd = regions[1][0], regions[4][0]
    out = []
    for i in range(count):
        e = d[md + i * metasz:md + (i + 1) * metasz]
        fmt, po, pl = struct.unpack('<III', e[4:16])
        tag, ch, rate, blk, bits = fmt & 3, (fmt >> 2) & 7, (fmt >> 5) & 0x3FFFF, (fmt >> 23) & 0xFF, fmt >> 31
        data = d[wd + po:wd + po + pl]
        if tag == 0 and bits:
            s = struct.unpack('<%dh' % (len(data) // 2), data[:len(data) // 2 * 2])
            mono = [sum(s[k:k + ch]) / (ch * 32768.0) for k in range(0, len(s) - ch + 1, ch)]
        elif tag == 0:
            mono = [sum(data[k + c] - 128 for c in range(ch)) / (ch * 128.0) for k in range(0, len(data) - ch + 1, ch)]
        elif tag == 2:
            mono = ms_adpcm(data, ch, (blk + 22) * ch)
        else:
            mono = []
        out.append((mono, rate))
    return out


# ---- shaping ----
def resample(x, rate):
    if rate == RATE:
        return list(x)
    n = int(len(x) * RATE / rate)
    out = []
    for k in range(n):
        t = k * rate / RATE
        i = int(t)
        f = t - i
        a = x[i] if i < len(x) else 0.0
        b = x[i + 1] if i + 1 < len(x) else 0.0
        out.append(a * (1 - f) + b * f)
    return out


def shape(x, rate, max_s=None, fade_in=0.003, fade_out=0.02, peak=0.7):
    """Resample to 48 kHz, cut to max_s (a longer fade then), fade the ends, set the peak."""
    y = resample(x, rate)
    if max_s and len(y) > max_s * RATE:
        y = y[:int(max_s * RATE)]
        fade_out = max(fade_out, 0.5)
    n_in, n_out = int(fade_in * RATE), int(fade_out * RATE)
    for i in range(min(n_in, len(y))):
        y[i] *= i / n_in
    for i in range(min(n_out, len(y))):
        y[-1 - i] *= i / n_out
    m = max((abs(v) for v in y), default=0.0)
    if m > 0:
        y = [v * peak / m for v in y]
    return y


def write(path, y):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    w = wave.open(path, 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(struct.pack('<%dh' % len(y), *[max(-32767, min(32767, int(v * 32767))) for v in y]))
    w.close()


def frames(rec, f0, f1):   # a recording's video frames f0..f1 (60 a second)
    return read_wav(rec, f0 / 60.0, f1 / 60.0)


# ---- recordings (made when missing) ----
def record_t81():
    rec = os.path.join(REC, 't81', 'rec.wav')
    if not os.path.exists(rec):
        os.makedirs(os.path.dirname(rec), exist_ok=True)
        spins = ['--spin=%d:1:3' % f for f in range(1300, 2401, 120)]
        subprocess.run([os.path.join(ROOT, 'build', 't81.exe'), '--headless', '--frames=4800',
                        '--userdata=' + os.path.join(REC, 't81', 'ud'), '--press=600:select:6', '--press=700:start:6',
                        *spins, '--wav=' + rec], cwd=ROOT, check=True, capture_output=True)
    return rec


def record_t3k():
    rec = os.path.join(REC, 't3k', 'rec.wav')
    if not os.path.exists(rec):
        ud = os.path.join(REC, 't3k', 'ud')
        os.makedirs(ud, exist_ok=True)
        open(os.path.join(ud, 't3k_settings.txt'), 'w').write('stick_mode = off\n')
        moves = ['--press=%d:left:4' % f for f in range(3300, 4201, 90)]
        subprocess.run([os.path.join(ROOT, 'build', 't3k.exe'), '--headless', '--frames=7000', '--userdata=' + ud,
                        '--poke=2:80113450:00000000:60', '--press=900:a:6', '--press=1300:a:6', '--press=1700:a:6',
                        '--press=2100:a:6', '--press=2500:a:6', *moves, '--wav=' + rec],
                        cwd=ROOT, check=True, capture_output=True)
    return rec


def main():
    global TXK
    for c in (os.path.join(ROOT, 'dist', 'TempestCollection', 'game', 'PCSB00494', 'assets'),
              os.path.join(ROOT, 'build', 'game', 'PCSB00494', 'assets'),
              os.path.join(ROOT, 'game', 'PCSB00494', 'assets')):
        if os.path.isdir(c):
            TXK = c
            break
    made = {}
    # Tempest (1981)
    r = record_t81()
    made['t81_move'] = shape(*frames(r, 1301.5, 1310))
    made['t81_launch'] = shape(*frames(r, 1727.5, 1800), fade_out=0.1)
    # Tempest 2000
    made['t2k_move'] = shape(*t2k_sample(29), peak=0.6)        # 'tink'
    made['t2k_launch'] = shape(*t2k_sample(26), fade_out=0.1)  # 'screeeam'
    # Tempest 3000
    r = record_t3k()
    made['t3k_move'] = shape(*frames(r, 3309.5, 3313))
    made['t3k_launch'] = shape(*frames(r, 3508, 3655), max_s=2.4)
    # Space Giraffe
    bank = sg_bank()
    for i, (x, rate) in enumerate(bank):
        if x:
            write(os.path.join(OUT, 'sg_bank', '%02d.wav' % i), shape(x, rate, fade_in=0, fade_out=0.005))
    made['sg_move'] = shape(*bank[SG_MOVE])
    made['sg_launch'] = shape(*bank[SG_LAUNCH], max_s=2.5)
    # TxK
    if TXK:
        made['txk_move'] = shape(*read_wav(os.path.join(TXK, 'tink.wav')), peak=0.6)
        made['txk_launch'] = shape(*read_wav(os.path.join(TXK, 'shipexplosion.wav')), max_s=2.5)
    else:
        print('TxK assets not found (game\\PCSB00494): no TxK sounds')
    # Tempest 4000
    t4 = os.path.join(STEAM, 'Tempest 4000', 'Win10', 'Assets', 'Sounds')
    made['t4k_move'] = shape(*read_wav(os.path.join(t4, 'clawmove.wav')), peak=0.6)
    made['t4k_launch'] = shape(*read_wav(os.path.join(t4, 'shipexplosion.wav')), max_s=2.5)
    for k, y in made.items():
        write(os.path.join(OUT, k + '.wav'), y)
        print('%-11s %.2f s' % (k, len(y) / RATE))
    print('->', OUT)


if __name__ == '__main__':
    main()

"""Claw feel: rebuild a game's claw position from a per-frame log and fit a physics model to it.

  python tools/claw_fit.py txk logs/claw/txk/run2.txt [--lanes 24] [--plot out.png]

txk log (txk.exe --peek=...:810EC514,810EC7F8,810E4970): frame, stick x, stick y, then (hex, float) pairs for
floor(position) and round(position) as lane ints, and the game's processed stick value. Together the two ints place
the claw in a half lane: [F, F + 0.5) when round == floor, [F + 0.5, F + 1) otherwise.

Model (per 60 Hz frame; lanes / frame; u = stick past the deadzone, 0..1):
  held:     v += accel * u ** expo * dir, |v| <= vmax                 (stick = force, not speed)
  released: v *= friction, plus a pull to the nearest lane centre: v += pull * (centre - p), and v *= damp near it
  p += v
Fitted by minimising how far the simulated position falls outside the observed half-lane interval each frame.
"""
import argparse
import math

import numpy as np
from scipy.optimize import minimize


def read_txk(path, lanes):
    rows = []
    for line in open(path):
        p = line.split()
        f, sx = int(p[0]), float(p[1])
        fl, rd = int(p[3], 16), int(p[5], 16)
        half = 2 * fl + (0 if rd == fl else 1)
        rows.append((f, sx, half))
    # unwrap the half-lane index (2 * lanes halves a turn)
    out, prev, off = [], None, 0
    for f, sx, h in rows:
        if prev is not None:
            d = h - prev
            if d > lanes:
                off -= 2 * lanes
            elif d < -lanes:
                off += 2 * lanes
        prev = h
        out.append((f, sx, (h + off) / 2.0))
    return np.array(out)


def segments(data, lead=3):
    """One segment per stick onset (from a few frames before it to the next onset). Dropped when the stick is held
    for 20 frames with no movement (the claw is dead / respawning)."""
    on = [i for i in range(1, len(data)) if data[i, 1] != 0 and data[i - 1, 1] == 0]
    out = []
    for k, i in enumerate(on):
        a = max(0, i - lead)
        b = on[k + 1] - lead if k + 1 < len(on) else len(data)
        s = data[a:b]
        # cut at a freeze: the stick held for 15 frames with no half-lane change (dead)
        run = 0
        for j in range(1, len(s)):
            run = run + 1 if (s[j, 1] != 0 and s[j, 2] == s[j - 1, 2]) else 0
            if run >= 15:
                s = s[: j - 15]
                break
        held = s[:, 1] != 0
        if held.sum() < 1 or len(s) < 10:
            continue
        if np.any(np.abs(np.diff(s[:, 2])) > 1.0):
            continue
        out.append(s)
    return out


def stick_u(sx, dz):
    a = abs(sx)
    return 0.0 if a <= dz else min(1.0, (a - dz) / (1 - dz))


def simulate(seg, prm, dz=0.084, sign=-1.0):
    accel, vmax, friction, pull, damp, expo = prm
    p = seg[0, 2] + 0.25   # the middle of the first observed half lane
    v = 0.0
    out = np.empty(len(seg))
    for i, (f, sx, lo) in enumerate(seg):
        u = stick_u(sx, dz)
        if u > 0:
            v += sign * math.copysign(1, sx) * accel * u ** expo
            v = max(-vmax, min(vmax, v))
        else:
            v *= friction
            centre = math.floor(p) + 0.5
            v += pull * (centre - p)
            if abs(centre - p) < 0.5:
                v *= damp
        p += v
        out[i] = p
    return out


def loss(prm, segs):
    if not (0 < prm[0] < 1 and 0 < prm[1] < 2 and 0 < prm[2] <= 1 and 0 <= prm[3] < 1 and 0 < prm[4] <= 1 and 0.1 < prm[5] < 3):
        return 1e9
    tot = 0.0
    for s in segs:
        p = simulate(s, prm)
        lo, hi = s[:, 2], s[:, 2] + 0.5
        err = np.where(p < lo, lo - p, np.where(p > hi, p - hi, 0.0))
        tot += float((err ** 2).sum())
    return tot


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game')
    ap.add_argument('log')
    ap.add_argument('--lanes', type=int, default=24)
    ap.add_argument('--plot')
    a = ap.parse_args()
    data = read_txk(a.log, a.lanes)
    segs = segments(data)
    print(f'{len(data)} frames, {len(segs)} segments: ' + ', '.join(f'{int(s[0, 0])}-{int(s[-1, 0])}' for s in segs))
    best = None
    for x0 in ([0.015, 0.3, 0.92, 0.01, 0.9, 1.0], [0.015, 0.3, 0.92, 0.01, 0.9, 0.5], [0.03, 0.3, 0.85, 0.02, 0.8, 0.7]):
        r = minimize(loss, x0, args=(segs,), method='Nelder-Mead', options={'maxiter': 4000, 'xatol': 1e-6, 'fatol': 1e-6})
        if best is None or r.fun < best.fun:
            best = r
    accel, vmax, friction, pull, damp, expo = best.x
    print(f"stick response u ** {expo:.3f}")
    print(f'loss {best.fun:.4f}')
    print(f'accel {accel:.5f} lanes/frame^2 per unit stick  (full stick: {vmax / accel:.1f} frames to top speed)')
    print(f'vmax {vmax:.4f} lanes/frame = {vmax * 60:.2f} lanes/s')
    print(f'friction {friction:.4f}/frame (speed half-life {math.log(0.5) / math.log(friction):.1f} frames)'
          if friction < 1 else 'friction 1')
    print(f'pull {pull:.5f}, damp {damp:.4f}')
    # report frames outside the interval
    worst = 0
    for s in segs:
        p = simulate(s, best.x)
        e = np.maximum(s[:, 2] - p, p - (s[:, 2] + 0.5)).clip(0)
        worst = max(worst, e.max())
    print(f'worst miss {worst:.3f} lanes')
    if a.plot:
        from PIL import Image, ImageDraw
        W, H = 1400, 150
        img = Image.new('RGB', (W, H * len(segs)), (255, 255, 255))
        d = ImageDraw.Draw(img)
        for k, sg in enumerate(segs):
            p = simulate(sg, best.x)
            y0 = k * H
            lo, hi = min(sg[:, 2].min(), p.min()), max(sg[:, 2].max() + 0.5, p.max())
            sy = lambda v: y0 + H - 10 - (v - lo) / max(hi - lo, 1e-6) * (H - 20)
            sx = lambda i: 10 + i * (W - 20) / max(1, len(sg) - 1)
            for i in range(len(sg)):
                d.line([(sx(i), sy(sg[i, 2])), (sx(i), sy(sg[i, 2] + 0.5))], fill=(150, 170, 210))
                if sg[i, 1] != 0:
                    d.point((sx(i), y0 + 4), fill=(40, 160, 40))
            d.line([(sx(i), sy(p[i])) for i in range(len(sg))], fill=(200, 40, 40), width=2)
            d.text((12, y0 + 6), f'{int(sg[0, 0])}-{int(sg[-1, 0])} stick {sg[sg[:, 1] != 0][0, 1]:+.2f}', fill=(0, 0, 0))
            d.line([(0, y0 + H - 1), (W, y0 + H - 1)], fill=(200, 200, 200))
        img.save(a.plot)

if __name__ == '__main__':
    main()

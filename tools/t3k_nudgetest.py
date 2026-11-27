"""Native T3K nudge test: scripted stick flicks in a --check run, the claw's resting position after each.

python -I tools/t3k_nudgetest.py [--lens 2,3,6,9,12,30] [--axis 90] [--off] [--period 120] [--log]
Each case: a flick of LEN frames (120 Hz fine steps in play) every PERIOD frames, four right then four left
(T3K_NUDGE_SCRIPT, src/t3k_runtime/native_live_host.h). Prints the claw's position at the end of each period
and the lane moves. Nudge assist on: every flick shorter than the hold time should move exactly one lane and
rest just past a whole number (floor = the lane).
"""
import argparse, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(length, axis, period, off, hold, keep):
    out = os.path.join(ROOT, 'logs', 't3k', 'nudge')
    os.makedirs(out, exist_ok=True)
    ud = os.path.join(out, 'ud_%s_%d' % ('off' if off else 'on', length))
    os.makedirs(ud, exist_ok=True)
    env = dict(os.environ, TEMPEST_CHECK_PATTERN='2', T3K_CLAW_LOG='1',
               T3K_NUDGE_SCRIPT='%d,%d,%d,%d' % (60, period, length, axis))
    sets = ['--set=t3k_nudge=%d' % (0 if off else 1)]
    if hold is not None:
        sets.append('--set=t3k_nudge_hold=%s' % hold)
    cmd = [os.path.join(ROOT, 'build', 't3k.exe'), '--check', '--fine-check-limit=%d' % ((60 + period * 9) * 2 // 3 + 20),
           '--winsize=640x480', '--userdata=' + ud] + sets
    p = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=150)
    rows = []
    for line in p.stdout.splitlines():
        m = re.match(r'claw f(\d+) p(\d+) step\S+ raw (-?\d+) sent (-?\d+) pos (-?[\d.]+) cmd (-?[\d.]+)', line)
        if m:
            rows.append((int(m.group(2)), int(m.group(3)), int(m.group(4)), float(m.group(5)), float(m.group(6))))
    if keep:
        with open(os.path.join(out, 'len%d_%s.txt' % (length, 'off' if off else 'on')), 'w') as f:
            f.write(p.stdout)
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lens', default='2,3,6,9,12,30')
    ap.add_argument('--axis', type=int, default=90)
    ap.add_argument('--period', type=int, default=80)
    ap.add_argument('--hold', default=None)
    ap.add_argument('--off', action='store_true', help='nudge assist off (the game as it was)')
    ap.add_argument('--log', action='store_true', help='keep each run log in logs/t3k/nudge')
    a = ap.parse_args()
    from concurrent.futures import ThreadPoolExecutor
    lens = [int(x) for x in a.lens.split(',')]
    with ThreadPoolExecutor(len(lens)) as pool:
        results = list(pool.map(lambda n: run(n, a.axis, a.period, a.off, a.hold, a.log), lens))
    for length, rows in zip(lens, results):
        by = {r[0]: r for r in rows}
        rests = []
        for k in range(9):
            frame = 60 + k * a.period - 1
            if frame in by:
                rests.append(by[frame][3])
        moves = ['%+.2f' % (b - c) for c, b in zip(rests, rests[1:])]
        floors = [int(x // 1) for x in rests]
        print('len %2d frames (%3.0f ms) axis %d %s: rests %s' % (length, length * 1000 / 120, a.axis,
              'OFF' if a.off else 'on ', ' '.join('%.3f' % x for x in rests)))
        print('    moves %s   lanes %s' % (' '.join(moves), floors))
    return 0


if __name__ == '__main__':
    sys.exit(main())

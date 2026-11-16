"""Tempest 2000 frame cost survey: runs t2k.exe in a window from each saved state with T2K_PERF on and tabulates
the cost of every screen (average and worst frame; emulation, run-ahead, our drawing by layer, CRT).

python -I tools/t2k_perfsurvey.py [--size 1280x800] [--frames 600] [--only name,name] [--out logs/perf]

Settings come from <out>/ud/settings.txt (copy the owner's build/userdata/settings.txt there for their look).
Frames are emulated frames (60 a second); the first 1.5 s of each run is left out (perf.cpp).
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))

# name, state (None = from power-on), what it is
SCREENS = [
    ('boot-attract', None, 'title, scores, attract demo from power-on'),
    ('modesel', 'logs/modesel.state', 'game select menu (ripple background)'),
    ('melt', 'logs/melt.state', 'melt transition'),
    ('play-l1', 'logs/play.state', 'in play, level 1'),
    ('play-shots', 'logs/shots/owner_shots.state', 'in play, enemies firing'),
    ('powerup', 'logs/pup2536.state', 'power-up climbing the lane'),
    ('zap', 'logs/zap62.state', 'Superzapper'),
    ('cheatplay', 'logs/cheatplay.state', 'in play (cheats on)'),
    ('demo-explode', 'logs/demo6400.state', 'attract demo, pixel explosion'),
    ('prewarp', 'logs/prewarp.state', 'level end, warp'),
    ('warp-stars', 'logs/owner_star.state', 'warp starfield between levels'),
    ('bonus1-ceiling', 'logs/bonus.state', 'bonus round 1 (cloud ceiling)'),
    ('bonus1-owner', 'logs/bonus1/line.state', 'bonus round 1, owner state'),
    ('bonus2-tunnel', 'logs/bonus16.state', 'bonus round 2 (tunnel)'),
    ('bonus3-psycho', 'logs/bonus32.state', 'bonus round 3 (psycho)'),
    ('duel', 'logs/duelplay.state', 'Tempest Duel in play'),
    ('team', 'logs/team.state', '2 player team'),
    ('gameover', 'logs/gameover.state', 'Game Over feedback'),
    ('hiscore', 'logs/hiscore.state', 'high-score entry'),
]


def parse(path):
    """-> (header dict, {name: (avg, worst)})"""
    head, rows = {}, {}
    if not os.path.exists(path):
        return head, rows
    for line in open(path):
        m = re.match(r'perf \S+: ([\d.]+) fps shown over ([\d.]+) s \| frame ([\d.]+) ms avg, ([\d.]+) ms worst, (\d+) over', line)
        if m:
            head = dict(fps=float(m[1]), secs=float(m[2]), avg=float(m[3]), worst=float(m[4]), over=int(m[5]))
            rows = {}
            continue
        m = re.match(r'  (.+?)\s+([\d.]+)( ms)? avg\s+([\d.]+)( ms)? worst', line)
        if m:
            rows[m[1].strip()] = (float(m[2]), float(m[4]))
    return head, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--size', default='1280x800')
    ap.add_argument('--frames', type=int, default=600)
    ap.add_argument('--only')
    ap.add_argument('--out', default='logs/perf')
    a = ap.parse_args()
    out = os.path.join(ROOT, a.out)
    os.makedirs(out, exist_ok=True)
    only = set(a.only.split(',')) if a.only else None
    exe = os.path.join(ROOT, 'build', 't2k.exe')
    rom = os.path.join(ROOT, 'Tempest 2000 (1994).jag')
    results = []
    for name, state, what in SCREENS:
        if only and name not in only:
            continue
        log = os.path.join(out, name + '.txt')
        if os.path.exists(log):
            os.remove(log)
        frames = a.frames if state else 1800
        args = [exe, rom, '--userdata=' + a.out + '/ud', '--winsize=' + a.size,
                '--winshot=%d:%s' % (frames, a.out + '/' + name + '.bmp')]
        if state:
            if not os.path.exists(os.path.join(ROOT, state)):
                print('skip %s: no %s' % (name, state))
                continue
            args.append('--state=' + state)
        env = dict(os.environ, T2K_PERF='1', T2K_PERF_SECONDS='0', T2K_PERF_FILE=log.replace('\\', '/'))
        subprocess.run(args, env=env, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
        head, rows = parse(log)
        results.append((name, what, head, rows))
        print('%-15s done' % name, flush=True)

    # table
    def g(rows, k, i=0):
        return rows.get(k, (0, 0))[i]
    print()
    print('%-15s %6s %6s %6s %4s | %6s %6s %6s | %6s %6s %6s %6s | %s' % (
        'screen', 'fps', 'avg', 'worst', '>16', 'emu', 'emuW', 'ahead', 'comp', 'compW', 'rmstr', 'crt', 'heaviest layer (avg / worst ms)'))
    for name, what, head, rows in results:
        if not head:
            print('%-15s  (no report)' % name)
            continue
        layers = [(k, v) for k, v in rows.items() if k.startswith('build ') or k.startswith('draw ')]
        layers.sort(key=lambda kv: -kv[1][0])
        top = ', '.join('%s %.2f/%.2f' % (k, v[0], v[1]) for k, v in layers[:2])
        print('%-15s %6.1f %6.2f %6.2f %4d | %6.2f %6.2f %6.2f | %6.2f %6.2f %6.2f %6.2f | %s' % (
            name, head['fps'], head['avg'], head['worst'], head['over'], g(rows, 'emulate'), g(rows, 'emulate', 1),
            g(rows, 'of it run-ahead'), g(rows, 'compose'), g(rows, 'compose', 1), g(rows, 'remaster (all)'),
            g(rows, 'crt + menu'), top))
    print('\ncounts (avg / worst a frame):')
    for name, what, head, rows in results:
        c = ', '.join('%s %d/%d' % (k, v[0], v[1]) for k, v in rows.items()
                      if k in ('lines', 'web triangles', 'solid triangles', 'stars', 'particles / dots', 'sprite rects') and v[1] > 0)
        print('  %-15s %s' % (name, c))


if __name__ == '__main__':
    sys.exit(main())

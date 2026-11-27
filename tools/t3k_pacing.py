"""T3K pacing check: does each scene keep its speed on a slower display?

python -I tools/t3k_pacing.py <base.csv> <case.csv> [<case.csv> ...]

Reads T3K_BENCH_CSV logs from `t3k.exe --live-check --novsync` runs (T3K_FAKE_REFRESH_HZ=N makes a present wait for
an N Hz display's next refresh, as vsync does). Splits each run into scenes (frames with the same fine-step division
and scene rate), and prints each scene's wall time against the base run (no fake display): a ratio of 1.00 = the
scene ran at its proper speed; 2.00 = half speed. `pres` = pictures presented in the scene.
"""
import csv
import os
import sys


def scenes(path):
    out, cur = [], None
    for r in csv.DictReader(open(path)):
        key = (r['divisions'], r['scene_hz'])
        if cur is None or cur['key'] != key:
            cur = {'key': key, 'first': int(r['frame']), 'n': 0, 'ms': 0.0, 'pres': 0}
            out.append(cur)
        cur['n'] += 1
        cur['ms'] += float(r['frame_ms'])
        cur['pres'] += int(r['presented'])
    return out


def main():
    base = scenes(sys.argv[1])
    cases = [(os.path.basename(p), scenes(p)) for p in sys.argv[2:]]
    head = f"{'frames':>11} {'div':>3} {'scnHz':>5} {'base ms':>8}"
    for name, _ in cases:
        head += f" | {name[:16]:>16} {'pres':>5}"
    print(head)
    for i, b in enumerate(base):
        line = f"{b['first']:>5}+{b['n']:<5} {b['key'][0]:>3} {b['key'][1]:>5} {b['ms']:8.0f}"
        for _, c in cases:
            if i < len(c) and c[i]['key'] == b['key']:
                line += f" | {c[i]['ms'] / max(b['ms'], 1):16.2f} {c[i]['pres']:5d}"
            else:
                line += f" | {'(differs)':>16} {'':>5}"
        print(line)
    total = sum(s['ms'] for s in base) / 1000
    print(f"total s: base {total:.1f}" + ''.join(f", {n} {sum(s['ms'] for s in c) / 1000:.1f}" for n, c in cases))


if __name__ == '__main__':
    main()

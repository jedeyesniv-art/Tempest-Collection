"""Step stick mode checks from a play state: each scenario's claw positions (--clawlog) and where it settles.

python -I tools/t3k_steptest.py [--state logs/t3k/states/play_f2900.state] [--f0 2900] [--exe build/t3k.exe]

Scenarios start 100 fields after the state. A nudge (a push of any length under the repeat delay) must land exactly
one lane on; three nudges three lanes; a wiggle R L R R L = +1 -1 +1 +1 -1. Prints the first field the claw moves.
"""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))


def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


STATE = arg('--state', 'logs/t3k/states/play_f2900.state')
F0 = int(arg('--f0', '2900'))
EXE = os.path.join(ROOT, arg('--exe', 'build/t3k.exe'))
OUT = os.path.join(ROOT, 'logs', 'steptest')
os.makedirs(OUT, exist_ok=True)
S = F0 + 100 + int(arg('--phase', '0'))   # first push (--phase N: N fields later, another picture phase)


def run(name, sticks, end=120):
    log = os.path.join(OUT, name + '.txt').replace('\\', '/')
    args = [EXE, '--headless', f'--loadstate={F0}:{STATE}', f'--frames={S + end}', '--userdata=logs/steptest/ud',
            f'--clawlog={S - 2}:{S + end - 1}:{log}']
    args += [f'--stick={f}:{x}:{n}' for f, x, n in sticks]
    if os.path.exists(log):
        os.remove(log)   # (a run that fails must not leave the last run's log to be read)
    subprocess.run(args, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(log):
        sys.exit(f'{name}: no claw log - the program did not run (an exe outside build/ has no system/ folder)')
    rows = [line.split() for line in open(log)]
    pos = [(int(r[0]), float(r[2])) for r in rows]
    start = pos[0][1]
    first = next((f for f, p in pos if abs(p - start) > 1e-4), None)
    return start, pos[-1][1], first, [p for _, p in pos]


def main():
    cases = [
        ('tap2', [(S, 1, 2)], 1), ('tap4', [(S, 1, 4)], 1), ('tap6', [(S, 1, 6)], 1), ('tap5', [(S, 1, 5)], 1), ('sweep8', [(S, 1, 8)], None),
        ('tap4_left', [(S, -1, 4)], -1),
        ('three', [(S, 1, 4), (S + 10, 1, 4), (S + 20, 1, 4)], 3),
        ('wiggle', [(S, 1, 4), (S + 15, -1, 4), (S + 30, 1, 4), (S + 45, 1, 4), (S + 60, -1, 4)], 1),
        ('half_push', [(S, 0.5, 4)], 1),
        ('hold30', [(S, 1, 30)], None),
    ]
    bad = 0
    for name, sticks, want in cases:
        start, end, first, path = run(name, sticks)
        moved = end - start
        ok = want is None or abs(moved - want) < 0.02
        bad += not ok
        print(f'{name:10s} {start:7.3f} -> {end:7.3f} ({moved:+.3f} lanes, want {want if want is not None else "-"})'
              f'  first move at +{first - S if first else "-"} fields  {"ok" if ok else "WRONG"}')
    print('all ok' if not bad else f'{bad} wrong')


if __name__ == '__main__':
    main()

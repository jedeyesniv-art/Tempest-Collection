"""Tempest 3000 speed benchmark + correctness check: the scripted route (title -> level 1 in play) headless,
mean PC time per field, and the md5 of the picture at field 2399 (emulated timing is deterministic, so speed
work must not change it).

python -I tools/t3k_bench.py [exe] [extra t3k.exe args...]
Baseline 2026-11-14 (nuon_rounds 220000, MPE3PacketHack Enabled): shot md5 b83090894009225d70164d162f95054d.
"""
import csv
import hashlib
import os
import statistics
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = 'b83090894009225d70164d162f95054d'


def main():
    exe = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 't3k.exe')
    extra = sys.argv[2:]
    logs = os.path.join(ROOT, 'logs', 't3k')
    os.makedirs(logs, exist_ok=True)
    shot = os.path.join(logs, 'bench2399.bmp')
    perf = os.path.join(logs, 'bench.csv')
    if os.path.exists(shot):
        os.remove(shot)
    args = [exe, '--headless', '--frames=2400', '--shot=2399:' + shot, '--perflog=' + perf,
            '--userdata=' + os.path.join(logs, 'ud'),
            '--press=400:start', '--press=900:a', '--press=1300:a', '--press=1700:a', '--press=2100:a'] + extra
    subprocess.run(args, capture_output=True, timeout=600)
    rows = list(csv.DictReader(open(perf)))
    run = [float(r['run_ms']) for r in rows[100:]]
    play = [float(r['run_ms']) for r in rows[2000:]]
    md5 = hashlib.md5(open(shot, 'rb').read()).hexdigest() if os.path.exists(shot) else 'no shot'
    s = sorted(run)
    print('ms per field: mean %.2f  p50 %.2f  p95 %.2f  max %.2f | in play mean %.2f | picture %s' % (
        statistics.mean(run), s[len(s) // 2], s[int(len(s) * .95)], s[-1], statistics.mean(play),
        'SAME' if md5 == BASELINE else 'DIFFERENT ' + md5))


if __name__ == '__main__':
    main()

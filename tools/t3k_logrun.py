"""Run t3k.exe headless from a saved play state with the core's investigation logs on (native renderer work).

python -I tools/t3k_logrun.py --state play_f2900 --fields 30 --out logs/t3k/x/run [options]

  --state NAME|PATH  a state from logs/t3k/states (play_f2700 level 1 start, play_f2900 level 1 play, play_f3000 level 2,
                     play_f3150 busier play) or a .state path named ..._f<field>.state (or give --at)
  --at F             the field the state was saved at (default: from the name)
  --fields N         fields to run after it (default 30)
  --span TYPE        NUANCE_SPANLOG=TYPE -> <out>_span.txt (frame buffer rows MPE0 read / wrote for that type)
  --entries          NUANCE_ENTRYLOG + ENTRYDATA -> <out>_ent.txt (every entry + the bus words MPE0 read for it)
  --pc PC[,PC..]     NUANCE_PCLOG (local pcs) -> into <out>_span.txt; runs interpreted (NOJIT) with --interp
  --interp LO-HI     library address range whose AOT modules are skipped (e.g. 806AF000-806AFA00); implies NOJIT
  --pictures P0:P1   PCLOG only for these pictures
  --mem A[,A..]      PCLOG_MEM local addresses
  --pix              PCLOG_PIX (vectors as pixel components)
  --only TYPES       NUANCE_ONLYTYPES (the emulator draws only these types)
  --shots F[,F..]    --shot at these fields -> <out>_s<F>.bmp
  --nativecheck F0:F1  ground-truth check (with --only, T3K_NATIVE_FORCE=1) -> <out>_check/
  --extra "ARGS"     more t3k.exe arguments
Field numbers continue from the state's (a state saved at 2900 runs 2901 .. 2900 + N).
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--state', required=True)
    ap.add_argument('--at', type=int)
    ap.add_argument('--fields', type=int, default=30)
    ap.add_argument('--out', required=True)
    ap.add_argument('--span')
    ap.add_argument('--entries', action='store_true')
    ap.add_argument('--pc')
    ap.add_argument('--interp')
    ap.add_argument('--pictures')
    ap.add_argument('--mem')
    ap.add_argument('--pix', action='store_true')
    ap.add_argument('--only')
    ap.add_argument('--shots')
    ap.add_argument('--nativecheck')
    ap.add_argument('--extra', default='')
    a = ap.parse_args()

    state = a.state
    if not os.path.exists(state):
        state = os.path.join(ROOT, 'logs', 't3k', 'states', a.state + ('' if a.state.endswith('.state') else '.state'))
    state = os.path.abspath(state)
    at = a.at
    if at is None:
        m = re.search(r'_f(\d+)', os.path.basename(state))
        if not m:
            sys.exit('give --at: no _f<field> in the state name')
        at = int(m.group(1))
    out = os.path.abspath(a.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    fwd = lambda p: p.replace('\\', '/')

    env = dict(os.environ)
    env['NUANCE_THREADS'] = '0'   # MPE0's logs in one stream, in order
    joblog = None
    if a.span or a.pc:
        joblog = out + '_span.txt'
        env['NUANCE_JOBLOG'] = fwd(joblog)
    if a.span:
        env['NUANCE_SPANLOG'] = a.span
    if a.entries:
        env['NUANCE_ENTRYLOG'] = fwd(out + '_ent.txt')
        env['NUANCE_ENTRYDATA'] = '1'
    if a.pc:
        env['NUANCE_PCLOG'] = a.pc
        if not a.interp:
            print('note: --pc without --interp: the PCLOG only fires in interpreted code')
    if a.interp:
        env['NUANCE_NOJIT'] = '1'
        env['NUANCE_AOT_SKIP'] = a.interp
    if a.pictures:
        env['NUANCE_PCLOG_PICTURES'] = a.pictures
    if a.mem:
        env['NUANCE_PCLOG_MEM'] = a.mem
    if a.pix:
        env['NUANCE_PCLOG_PIX'] = '1'
    if a.only:
        env['NUANCE_ONLYTYPES'] = a.only
        env['T3K_NATIVE_FORCE'] = '1'

    last = at + a.fields
    args = [os.path.join(ROOT, 'build', 't3k.exe'), '--headless', '--frames=%d' % (last + 1),
            '--userdata=' + fwd(out + '_ud'), '--loadstate=%d:%s' % (at, fwd(state))]
    if a.shots:
        for f in a.shots.split(','):
            args.append('--shot=%s:%s' % (f, fwd(out + '_s%s.bmp' % f)))
    if a.nativecheck:
        args.append('--nativecheck=%s:%s' % (a.nativecheck, fwd(out + '_check')))
    args += a.extra.split()
    with open(out + '_out.txt', 'w') as log:
        r = subprocess.run(args, env=env, stdout=log, stderr=subprocess.STDOUT, cwd=ROOT)
    print('fields %d..%d, exit %d; %s' % (at + 1, last, r.returncode, ', '.join(
        p for p in (joblog, env.get('NUANCE_ENTRYLOG'), out + '_out.txt') if p)))
    if a.nativecheck:
        tail = open(out + '_out.txt').read().strip().splitlines()
        print('\n'.join(l for l in tail if l.startswith('check:')))


if __name__ == '__main__':
    main()

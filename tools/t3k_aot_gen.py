"""Generate build_aot\\*.cpp from a module catalogue (NUANCE_AOT_COLLECT dumps: m<src>_<base>_<len>.txt + .bin).

python -I tools/t3k_aot_gen.py [catalog_dir] [out_dir]     (defaults: aot_catalog, build_aot)

Skips sources that are already up to date; removes generated files whose dump is gone. Prints a summary.
Both directories hold game code (dumps / translations): they are git-ignored and must never be committed.
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    cat = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'aot_catalog')
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'build_aot')
    os.makedirs(out, exist_ok=True)
    want = set()
    made = skipped = failed = 0
    for dump in sorted(glob.glob(os.path.join(cat, 'm*.txt'))):
        name = 'aot_' + os.path.basename(dump)[1:-4] + '.cpp'
        want.add(name)
        dst = os.path.join(out, name)
        if os.path.exists(dst) and os.path.getmtime(dst) >= max(os.path.getmtime(dump),
                                                                  os.path.getmtime(os.path.join(ROOT, 'tools', 't3k_aot.py'))):
            skipped += 1
            continue
        r = subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools', 't3k_aot.py'), dump, dst],
                           capture_output=True, text=True)
        if r.returncode:
            failed += 1
            print('FAILED %s: %s' % (os.path.basename(dump), (r.stderr or r.stdout).strip().splitlines()[-1]))
        else:
            made += 1
    removed = 0
    for f in glob.glob(os.path.join(out, 'aot_*.cpp')):
        if os.path.basename(f) not in want:
            os.remove(f)
            removed += 1
    print('%d generated, %d up to date, %d failed, %d removed (%d modules)' % (made, skipped, failed, removed, len(want)))


if __name__ == '__main__':
    main()

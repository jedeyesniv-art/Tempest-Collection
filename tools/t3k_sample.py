"""Symbolize a T3K_SAMPLE file (count, module, offset) with llvm-symbolizer and print the hottest functions
and lines.

python -I tools/t3k_sample.py samples.txt [top_n]
Needs the module's PDB next to it (the debug core: tools\\build_nuance_dbg.bat, run from a folder holding that DLL).
"""
import collections
import subprocess
import sys

SYMBOLIZER = r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\llvm-symbolizer.exe'


def main():
    rows = []
    for line in open(sys.argv[1]):
        n, mod, off = line.rstrip('\n').split('\t')
        rows.append((int(n), mod, int(off, 16)))
    top = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    total = sum(r[0] for r in rows)
    by_mod = collections.Counter()
    for n, mod, _ in rows:
        by_mod[mod.split('\\')[-1]] += n
    print('%d samples' % total)
    for mod, n in by_mod.most_common():
        print('  %5.1f%% %s' % (100.0 * n / total, mod))
    funcs, lines = collections.Counter(), collections.Counter()
    by_module = collections.defaultdict(list)
    for n, mod, off in rows:
        if mod != 'jit':
            by_module[mod].append((n, off))
    for mod, items in by_module.items():
        inp = '\n'.join('0x%x' % off for _, off in items) + '\n'   # module offsets (--relative-address)
        out = subprocess.run([SYMBOLIZER, '--obj=' + mod, '--no-inlines', '--relative-address'],
                             input=inp, capture_output=True, text=True).stdout
        blocks = out.strip().split('\n\n')
        short = mod.split('\\')[-1]
        for (n, _), b in zip(items, blocks):
            parts = b.split('\n')
            fn = parts[0] if parts else '?'
            src = parts[1].split('\\')[-1] if len(parts) > 1 else '?'
            funcs['%s!%s' % (short, fn)] += n
            lines['%s  (%s)' % (src, fn[:40])] += n
    print('\nfunctions:')
    for f, n in funcs.most_common(top):
        print('  %5.1f%% %s' % (100.0 * n / total, f[:110]))
    print('\nlines:')
    for f, n in lines.most_common(top):
        print('  %5.1f%% %s' % (100.0 * n / total, f[:110]))


if __name__ == '__main__':
    main()

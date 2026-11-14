"""Search decoded module dumps (aot_catalog/m<src>_<base>_<len>.txt, the longest per module) for operand values.

python -I tools/t3k_immsearch.py <src1,src2,...> <hexvalue1,hexvalue2,...>
Prints every instruction (handler name + fields) with a field equal to one of the values.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def handler_names():
    s = open(os.path.join(ROOT, 'third_party', 'NuanceResurrection', 'src', 'mpe.cpp'), encoding='utf-8').read()
    i = s.index('const NuanceHandler nuanceHandlers[] =')
    body = re.sub(r'//[^\n]*', '', s[s.index('{', i) + 1:s.index('};', i)])
    return [x.strip() for x in body.split(',') if x.strip()]


def main():
    srcs = sys.argv[1].upper().split(',')
    vals = {int(v, 16) for v in sys.argv[2].split(',')}
    names = handler_names()
    for src in srcs:
        files = glob.glob(os.path.join(ROOT, 'aot_catalog', 'm%s_*.txt' % src))
        if not files:
            print(src, 'not in the catalogue')
            continue
        f = max(files, key=lambda p: int(p.rsplit('_', 1)[1][:-4]))
        pc = None
        hits = []
        for line in open(f):
            p = line.split()
            if p[0] == 'P':
                pc = p[1]
            elif p[0] == 'N':
                fields = p[2:7]
                for fi, tok in enumerate(fields[1:], 1):
                    if tok[0] in 'DME?':
                        continue
                    try:
                        v = int(tok, 16) & 0xFFFFFFFF
                    except ValueError:
                        continue
                    if v in vals:
                        hits.append('%s %s field%d=%X  (%s)' % (pc, names[int(p[1])], fi, v, ' '.join(fields[1:])))
        print('%s (%s): %d hits' % (src, os.path.basename(f), len(hits)))
        for h in hits[:30]:
            print('   ', h)


if __name__ == '__main__':
    main()

"""Tempest 3000 renderer code modules from a NUANCE_JOBLOG log: consecutive 'codeload' DMA chunks for the same
target MPE (source and destination both continuing) are merged into one module load; prints the distinct modules
(source start, length, destination), how often each is loaded, by which MPEs, and in which field range.

python -I tools/t3k_modules.py jobs.txt [from_field]
"""
import collections
import sys


def main():
    f0 = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    loads = []          # (field, target mpe, src, dst, len)
    cur = None
    for line in open(sys.argv[1]):
        p = line.split()
        if len(p) < 11 or p[1] != 'codeload':
            continue
        field = int(p[0][1:])
        mpe = p[2]
        src, dst, ln = int(p[6], 16), int(p[8], 16), int(p[10])
        if cur and cur[1] == mpe and cur[2] + cur[4] == src and cur[3] + cur[4] == dst:
            cur[4] += ln
        else:
            if cur:
                loads.append(tuple(cur))
            cur = [field, mpe, src, dst, ln]
    if cur:
        loads.append(tuple(cur))
    loads = [l for l in loads if l[0] >= f0]
    mods = collections.defaultdict(lambda: [0, set(), 10 ** 9, 0, set()])
    for field, mpe, src, dst, ln in loads:
        m = mods[(src, ln)]
        m[0] += 1
        m[1].add(mpe)
        m[2] = min(m[2], field)
        m[3] = max(m[3], field)
        m[4].add(dst)
    fields = (max(l[0] for l in loads) - min(l[0] for l in loads) + 1) if loads else 1
    print('%d module loads, %d distinct modules, over %d fields' % (len(loads), len(mods), fields))
    for (src, ln), (n, mpes, a, b, dsts) in sorted(mods.items(), key=lambda kv: -kv[1][0]):
        print('  src %08X len %5d  %6d loads (%5.1f a field)  %s  fields %d-%d  dst %s' % (
            src, ln, n, n / fields, ','.join(sorted(mpes)), a, b, ','.join('%08X' % d for d in sorted(dsts))[:40]))


if __name__ == '__main__':
    main()

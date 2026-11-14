"""From a NUANCE_JOBLOG log, the (module, local base) pairs worth AOT-compiling: full module loads (merged DMA chunks)
of at least min_len bytes, most loaded first, as t3k.exe --aotdump arguments (dumped at field F into dir).

python -I tools/t3k_aot_targets.py jobs.txt F dir [min_len] [max_modules]
A global destination ($20800000 + n * $800000 = MPE n's local memory seen from outside) becomes the local address.
"""
import collections
import os
import sys


def local(dst):
    if 0x20000000 <= dst < 0x22000000:
        return 0x20000000 | (dst & 0x007FFFFF)
    return dst


def main():
    log, field, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    min_len = int(sys.argv[4]) if len(sys.argv) > 4 else 700
    max_mods = int(sys.argv[5]) if len(sys.argv) > 5 else 40
    loads, cur = [], None
    for line in open(log):
        p = line.split()
        if len(p) < 11 or p[1] != 'codeload':
            continue
        mpe, src, dst, ln = p[2], int(p[6], 16), int(p[8], 16), int(p[10])
        if cur and cur[0] == mpe and cur[1] + cur[3] == src and cur[2] + cur[3] == dst:
            cur[3] += ln
        else:
            if cur:
                loads.append(tuple(cur))
            cur = [mpe, src, dst, ln]
    if cur:
        loads.append(tuple(cur))
    count = collections.Counter()
    for mpe, src, dst, ln in loads:
        if ln >= min_len and (src >> 24) == 0x80:
            count[(src, ln, local(dst))] += 1
    args = []
    for (src, ln, base), n in count.most_common(max_mods):
        path = os.path.join(out, 'm%08X_%08X.txt' % (src, base)).replace('\\', '/')
        args.append('--aotdump=%d:0:%08X:%d:%08X:%s' % (field, src, ln, base, path))
    print(' '.join(args))


if __name__ == '__main__':
    main()

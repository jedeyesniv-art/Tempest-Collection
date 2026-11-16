"""Search TxK guest memory dumps (txk.exe --memdump=F:file) for values that move with a scripted input.

  python tools/txk_memsearch.py d1.bin d2.bin ... --expect "1<2, 2=3, 3<4, 5<4" [--float | --int] [--show N]

Dumps are compared at the same guest address (ranges present in every dump). --expect lists relations between dump
numbers (1-based): a<b, a>b, a=b, a!=b. Candidates satisfying all of them are printed (address, values per dump).
A relation set is matched both as written and mirrored (every < and > swapped) since the sign of a position is unknown.
"""
import argparse
import re
import struct
import sys

import numpy as np


def load(path):
    data = open(path, 'rb').read()
    assert data[:4] == b'VMEM', path
    at, ranges = 4, {}
    while at < len(data):
        addr, ln = struct.unpack_from('<II', data, at)
        at += 8
        ranges[addr] = np.frombuffer(data, dtype=np.uint8, count=ln, offset=at)
        at += ln
    return ranges


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dumps', nargs='+')
    ap.add_argument('--expect', required=True)
    ap.add_argument('--int', action='store_true', help='int32 instead of float32')
    ap.add_argument('--show', type=int, default=60)
    ap.add_argument('--range', default='', help='only values with |v| below this (floats)')
    ap.add_argument('--skip', default='60000000,70000000', help='ranges (start, hex) left out: GPU memory')
    ap.add_argument('--minabs', type=float, default=0.0, help='only values with |v| at or above this, or exactly 0')
    a = ap.parse_args()
    dumps = [load(p) for p in a.dumps]
    rels = []
    for part in a.expect.split(','):
        m = re.fullmatch(r'\s*(\d+)\s*(<|>|=|!=)\s*(\d+)\s*', part)
        if not m:
            sys.exit('bad relation ' + part)
        rels.append((int(m.group(1)) - 1, m.group(2), int(m.group(3)) - 1))
    skip = {int(x, 16) for x in a.skip.split(',') if x}
    common = [r for r in dumps[0] if r not in skip and all(r in d and len(d[r]) == len(dumps[0][r]) for d in dumps)]
    dt = np.int32 if a.int else np.float32
    found = []
    for base in common:
        vals = [d[base][: len(d[base]) // 4 * 4].view(dt) for d in dumps]
        ok_fin = np.ones(len(vals[0]), bool)
        if not a.int:
            for v in vals:
                ok_fin &= np.isfinite(v)
                if a.range:
                    ok_fin &= np.abs(v) < float(a.range)
                if a.minabs:
                    ok_fin &= (np.abs(v) >= a.minabs) | (v == 0)
        for mirror in (False, True):
            m = ok_fin.copy()
            for i, op, j in rels:
                if mirror and op in '<>':
                    op = '>' if op == '<' else '<'
                x, y = vals[i], vals[j]
                if op == '<': m &= x < y
                elif op == '>': m &= x > y
                elif op == '=': m &= x == y
                else: m &= x != y
            for k in np.nonzero(m)[0]:
                found.append((base + 4 * int(k), [float(v[k]) if not a.int else int(v[k]) for v in vals]))
    found.sort()
    print(f'{len(found)} candidates')
    for addr, vs in found[: a.show]:
        print(f'{addr:08X} ' + ' '.join(f'{v:.5g}' if isinstance(v, float) else str(v) for v in vs))


if __name__ == '__main__':
    main()

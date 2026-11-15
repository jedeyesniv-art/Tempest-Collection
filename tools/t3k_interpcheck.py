"""Tempest 3000 native renderer research: can consecutive pictures be interpolated? Pairs the display list entries of
picture n and n + 1 and reports how stable they are.

python -I tools/t3k_interpcheck.py <entrylog with NUANCE_ENTRYDATA=1> [first field] [last field]

Per consecutive picture pair (both inside the field range):
  - slots: same list address with the same type (and for 00710001 the same shape pointer w7) in both pictures;
  - motion: per stable slot, how far the screen position (w0) moved, in 360x240 pixels;
  - streams: for vector shapes, whether the shape stream (as fetched) has the same structure (commands and vertex
    count) in both pictures, and how far its vertices moved (runtime shapes such as the web are rebuilt each picture).
"""
import collections
import statistics
import sys


def sext16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def load(path, f0, f1):
    pics = collections.OrderedDict()   # picture -> {'field', 'entries': OrderedDict(src -> words), 'mem'}
    for line in open(path):
        t = line.split()
        if len(t) < 21 or not t[0].startswith('g'):
            continue
        field, pic, src, mask = int(t[1][1:]), int(t[2][1:]), int(t[3], 16), int(t[4][1:], 16)
        if not (f0 <= field <= f1):
            continue
        p = pics.setdefault(pic, {'field': field, 'entries': collections.OrderedDict(), 'mem': {}})
        words = [int(x, 16) for x in t[5:21]]
        if 'data' in t:
            for chunk in t[t.index('data') + 1:]:
                parts = chunk[1:].split(':')
                base = int(parts[0], 16)
                for i, w in enumerate(parts[1:]):
                    p['mem'][base + 4 * i] = int(w, 16)
        if mask & 5:                       # drawn by MPE0 / MPE2 (the drawing pass)
            p['entries'][src] = words
    return pics


def stream(mem, ptr, limit=4000):
    """(structure, vertices): structure = command words / 'V' per vertex; None if not fully fetched."""
    struct, verts = [], []
    for _ in range(limit):
        w = mem.get(ptr)
        if w is None:
            return None, None
        ptr += 4
        if w >> 16 == 0x8000:
            cmd = w & 0xF
            struct.append('C%X' % cmd)
            if cmd == 1:
                return tuple(struct), verts
            if cmd in (0, 3, 5, 6, 9):
                ptr += 4
            elif cmd == 4:
                ptr += 8
        else:
            struct.append('V')
            verts.append((sext16(w), sext16(w >> 16)))
    return None, None


def main():
    path = sys.argv[1]
    f0 = int(sys.argv[2]) if len(sys.argv) > 2 else 2300
    f1 = int(sys.argv[3]) if len(sys.argv) > 3 else 3800
    pics = load(path, f0, f1)
    keys = list(pics)
    stats = collections.Counter()
    moves = collections.defaultdict(list)
    vmoves = []
    jumps = []
    for a, b in zip(keys, keys[1:]):
        if b != a + 1:
            continue
        pa, pb = pics[a], pics[b]
        stats['pairs'] += 1
        ta = [w[15] for w in pa['entries'].values()]
        tb = [w[15] for w in pb['entries'].values()]
        stats['same type sequence'] += ta == tb
        for src, wa in pa['entries'].items():
            wb = pb['entries'].get(src)
            ty = wa[15]
            stats['entries'] += 1
            if wb is None or wb[15] != ty:
                stats['slot changed type / vanished'] += 1
                continue
            if ty == 0x00710001 and wa[7] != wb[7]:
                stats['vector: other shape in the slot'] += 1
                continue
            stats['stable'] += 1
            dx = sext16(wb[0] >> 16) - sext16(wa[0] >> 16)
            dy = sext16(wb[0]) - sext16(wa[0])
            d = (dx * dx + dy * dy) ** 0.5
            moves['%08X' % ty].append(d)
            if d > 40 and len(jumps) < 12:
                jumps.append((pa['field'], '%08X' % src, '%08X' % ty, '%08X -> %08X' % (wa[0], wb[0])))
            if ty == 0x00710001:
                sa, va = stream(pa['mem'], wa[7])
                sb, vb = stream(pb['mem'], wb[7])
                if sa is None or sb is None:
                    stats['vector: stream not fetched'] += 1
                elif sa != sb:
                    stats['vector: stream structure changed'] += 1
                else:
                    stats['vector: same structure'] += 1
                    if va != vb:
                        stats['vector: vertices moved'] += 1
                        vmoves.append(max(abs(x0 - x1) + abs(y0 - y1) for (x0, y0), (x1, y1) in zip(va, vb)))
    print('fields %d-%d: %d pictures' % (f0, f1, len(keys)))
    for k, v in stats.items():
        print('  %-36s %d' % (k, v))
    print('screen position change per stable slot (360x240 px): median / p95 / max')
    for ty, m in sorted(moves.items(), key=lambda kv: -len(kv[1])):
        s = sorted(m)
        print('  %s %6d: %6.2f %6.2f %7.2f' % (ty, len(s), statistics.median(s), s[int(len(s) * .95)], s[-1]))
    if vmoves:
        s = sorted(vmoves)
        print('runtime shape vertices (shape units, max per shape): median %d p95 %d max %d' % (
            statistics.median(s), s[int(len(s) * .95)], s[-1]))
    print('jumps > 40 px:')
    for j in jumps:
        print('  ', *j)


if __name__ == '__main__':
    main()

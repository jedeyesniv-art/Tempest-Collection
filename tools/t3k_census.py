"""Tempest 3000 display list census (native renderer research): summarise NUANCE_ENTRYLOG files.

python -I tools/t3k_census.py <entrylog> [more logs...]

Log lines (core fbprobe.cpp): g<group> f<field> p<picture> <src> m<readers> <16 words> [wb <16 words>] mods <lib srcs>
A picture = all groups with the same picture counter (an MPE1 pre-pass that projects objects and writes them back,
then the pass all three renderers draw). Per (picture, address) the LAST read is what was drawn.

Prints: per entry type - instances, pictures, readers, written back, modules, and per word how many distinct values
(constant words shown); then the distinct pictures' type sequences ("screens") with counts and field ranges.
"""
import collections
import sys


def collapse(loads):
    """Code loads 'src@local offset' (chunks of up to 256 bytes) -> whole modules 'src@offset': chunks with the same
    src - offset belong to one load; the module is named by its lowest chunk. Old logs (plain src) pass through."""
    if not loads or '@' not in loads[0]:
        return loads
    first = {}
    for ld in loads:
        s, o = ld.split('@')
        s, o = int(s, 16), int(o, 16)
        k = s - o
        if k not in first or s < first[k][0]:
            first[k] = (s, o)
    return ['%08X@%X' % v for v in sorted(first.values(), key=lambda v: v[1])]


def parse(path):
    pics = collections.OrderedDict()   # (file, picture) -> {src: rec}
    for line in open(path):
        t = line.split()
        if len(t) < 21 or not t[0].startswith('g'):
            continue
        field, pic, src, mask = int(t[1][1:]), int(t[2][1:]), t[3], int(t[4][1:], 16)
        words = [int(x, 16) for x in t[5:21]]
        rest = t[21:]
        wb = None
        if rest and rest[0] == 'wb':
            wb = [int(x, 16) for x in rest[1:17]]
            rest = rest[17:]
        mods = collapse(rest[1:]) if rest and rest[0] == 'mods' else []
        p = pics.setdefault((path, pic), collections.OrderedDict())
        r = p.get(src)
        if r is None:
            r = p[src] = {'field': field, 'mask': 0, 'words': words, 'wb': None, 'mods': [], 'order': len(p)}
        r['mask'] |= mask
        r['words'] = words          # the last read = what was drawn
        if wb:
            r['wb'] = wb
        for m in mods:
            if m not in r['mods']:
                r['mods'].append(m)
    return pics


def main():
    pics = collections.OrderedDict()
    for path in sys.argv[1:]:
        pics.update(parse(path))
    types = collections.defaultdict(lambda: {'n': 0, 'pics': set(), 'mask': collections.Counter(), 'wb': 0,
                                             'mods': collections.Counter(), 'vals': [collections.Counter() for _ in range(15)],
                                             'fields': [10 ** 9, 0]})
    screens = collections.OrderedDict()
    for key, p in pics.items():
        seq = []
        for src, r in sorted(p.items(), key=lambda kv: kv[1]['order']):
            ty = r['words'][15]
            d = types[ty]
            d['n'] += 1
            d['pics'].add(key)
            d['mask'][r['mask']] += 1
            d['wb'] += r['wb'] is not None
            d['mods'][' '.join(r['mods']) or '-'] += 1
            for i in range(15):
                d['vals'][i][r['words'][i]] += 1
            d['fields'][0] = min(d['fields'][0], r['field'])
            d['fields'][1] = max(d['fields'][1], r['field'])
            seq.append('%08X' % ty)
        f = min(r['field'] for r in p.values())
        sig = tuple(t for t in seq if t != '000000FF')
        s = screens.setdefault(frozenset(sig), {'n': 0, 'fields': [], 'example': sig, 'file': key[0]})
        s['n'] += 1
        s['fields'].append(f)
    print('%d pictures, %d entry types\n' % (len(pics), len(types)))
    for ty, d in sorted(types.items(), key=lambda kv: -kv[1]['n']):
        print('type %08X: %d entries in %d pictures, fields %d-%d, readers %s, written back %d' % (
            ty, d['n'], len(d['pics']), d['fields'][0], d['fields'][1],
            ' '.join('m%X:%d' % kv for kv in d['mask'].most_common()), d['wb']))
        for m, c in d['mods'].most_common(6):
            print('    modules %s: %d' % (m, c))
        words = []
        for i in range(15):
            v = d['vals'][i]
            words.append('w%d=%08X' % (i, next(iter(v))) if len(v) == 1 else 'w%d:%d' % (i, len(v)))
        print('    ' + ' '.join(words))
    print('\nscreens (distinct sets of entry types per picture):')
    for sig, s in sorted(screens.items(), key=lambda kv: -kv[1]['n']):
        fl = s['fields']
        print('%6d pictures, fields %d-%d (%s): %s' % (s['n'], min(fl), max(fl), s['file'].split('/')[-1].split('\\')[-1],
                                                     ' '.join(t[-4:] for t in s['example'])))


if __name__ == '__main__':
    main()

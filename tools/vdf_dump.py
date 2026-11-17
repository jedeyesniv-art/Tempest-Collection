"""Dump Steam's binary VDF (shortcuts.vdf) as indented text - a check on the launcher's add-to-Steam.

python -I tools/vdf_dump.py <shortcuts.vdf>
"""
import struct
import sys


def parse(d, at, depth, out):
    while at < len(d):
        t = d[at]
        at += 1
        if t == 8:
            return at
        z = d.index(b'\0', at)
        key = d[at:z].decode('utf-8', 'replace')
        at = z + 1
        pad = '  ' * depth
        if t == 0:
            out.append('%s%s {' % (pad, key))
            at = parse(d, at, depth + 1, out)
            out.append(pad + '}')
        elif t == 1:
            e = d.index(b'\0', at)
            out.append('%s%s = "%s"' % (pad, key, d[at:e].decode('utf-8', 'replace')))
            at = e + 1
        elif t == 2:
            v = struct.unpack('<i', d[at:at + 4])[0]
            out.append('%s%s = %d (unsigned %d)' % (pad, key, v, v & 0xFFFFFFFF))
            at += 4
        elif t in (3, 6):
            out.append('%s%s = <4 bytes %s>' % (pad, key, d[at:at + 4].hex()))
            at += 4
        elif t in (7, 10):
            out.append('%s%s = <8 bytes %s>' % (pad, key, d[at:at + 8].hex()))
            at += 8
        else:
            raise SystemExit('unknown type %d at %d' % (t, at - 1))
    raise SystemExit('ran off the end')


def main():
    d = open(sys.argv[1], 'rb').read()
    out = []
    end = parse(d, 0, 0, out)
    print('\n'.join(out))
    print('-- %d bytes, parsed to %d%s' % (len(d), end, '' if end == len(d) else ' (TRAILING BYTES)'))


if __name__ == '__main__':
    main()

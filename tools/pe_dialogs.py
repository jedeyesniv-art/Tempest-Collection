"""List the dialog resources (RT_DIALOG) of a PE file: id, title and controls (class, id, text).

python -I tools/pe_dialogs.py <exe>
Plain struct parsing (no pefile); handles DLGTEMPLATE and DLGTEMPLATEEX.
"""
import struct
import sys


def sections(d):
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    magic = struct.unpack_from('<H', d, pe + 24)[0]
    dd = pe + 24 + (96 if magic == 0x10B else 112)
    rsrc_rva = struct.unpack_from('<I', d, dd + 2 * 8)[0]
    out = []
    for i in range(nsec):
        s = pe + 24 + opt + 40 * i
        va, vs, raw_size, raw = struct.unpack_from('<IIII', d, s + 8)[0:1] + struct.unpack_from('<I', d, s + 8)[0:0] + \
            (struct.unpack_from('<I', d, s + 8)[0],) * 0 + struct.unpack_from('<III', d, s + 12)  # va, (raw_size, raw)
        out.append((d[s:s + 8].rstrip(b'\0'), struct.unpack_from('<I', d, s + 12)[0], struct.unpack_from('<I', d, s + 8)[0],
                    struct.unpack_from('<I', d, s + 20)[0]))
    return rsrc_rva, out


def rva2off(secs, rva):
    for _, va, vs, raw in secs:
        if va <= rva < va + max(vs, 1):
            return rva - va + raw
    raise ValueError(hex(rva))


def wstr(d, o):
    e = o
    while d[e:e + 2] != b'\0\0':
        e += 2
    return d[o:e].decode('utf-16le'), e + 2


def sz_or_ord(d, o):
    w = struct.unpack_from('<H', d, o)[0]
    if w == 0:
        return '', o + 2
    if w == 0xFFFF:
        return '#%d' % struct.unpack_from('<H', d, o + 2)[0], o + 4
    return wstr(d, o)


def align4(o, base):
    return base + ((o - base + 3) & ~3)


CLS = {0x80: 'BUTTON', 0x81: 'EDIT', 0x82: 'STATIC', 0x83: 'LISTBOX', 0x84: 'SCROLLBAR', 0x85: 'COMBOBOX'}


def dialog(d, o):
    base = o
    ex = struct.unpack_from('<HH', d, o) == (1, 0xFFFF)
    if ex:
        _, _, helpid, exstyle, style, n, x, y, cx, cy = struct.unpack_from('<HHIIIHhhhh', d, o)
        o += 26
    else:
        style, exstyle, n, x, y, cx, cy = struct.unpack_from('<IIHhhhh', d, o)
        o += 18
    menu, o = sz_or_ord(d, o)
    cls, o = sz_or_ord(d, o)
    title, o = wstr(d, o)
    if style & 0x40:   # DS_SETFONT
        o += 6 if ex else 2
        _, o = wstr(d, o)
    ctrls = []
    for _ in range(n):
        o = align4(o, base)
        if ex:
            _, _, _, x, y, cx, cy, cid = struct.unpack_from('<IIIhhhhI', d, o)
            o += 24
        else:
            _, _, x, y, cx, cy, cid = struct.unpack_from('<IIhhhhH', d, o)
            o += 18
        w = struct.unpack_from('<H', d, o)[0]
        if w == 0xFFFF:
            c = CLS.get(struct.unpack_from('<H', d, o + 2)[0], '?')
            o += 4
        else:
            c, o = wstr(d, o)
        t, o = sz_or_ord(d, o)
        extra = struct.unpack_from('<H', d, o)[0]
        o += 2 + extra
        ctrls.append((c, cid, t))
    return title, (cx, cy), ctrls


def main():
    d = open(sys.argv[1], 'rb').read()
    rsrc, secs = sections(d)
    root = rva2off(secs, rsrc)

    def entries(o):
        nn, ni = struct.unpack_from('<HH', d, o + 12)
        for i in range(nn + ni):
            name, off = struct.unpack_from('<II', d, o + 16 + 8 * i)
            yield name, off

    for tid, toff in entries(root):
        if tid != 5:   # RT_DIALOG
            continue
        for did, doff in entries(root + (toff & 0x7FFFFFFF)):
            for _, loff in entries(root + (doff & 0x7FFFFFFF)):
                rva, size = struct.unpack_from('<II', d, root + loff)
                title, sz, ctrls = dialog(d, rva2off(secs, rva))
                print('dialog %s "%s" %dx%d' % (did if not did & 0x80000000 else 'named', title, *sz))
                for c in ctrls:
                    print('   %-10s id %-6d %s' % c)


if __name__ == '__main__':
    main()

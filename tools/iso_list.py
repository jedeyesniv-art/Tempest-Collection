"""List the files of an ISO9660 image (2048-byte sectors): path, LBA, size."""
import struct
import sys


def read_dir(f, lba, size, path, out):
    f.seek(lba * 2048)
    data = f.read(size)
    i = 0
    while i < len(data):
        n = data[i]
        if n == 0:  # padding to the next sector
            i = (i // 2048 + 1) * 2048
            continue
        rec = data[i:i + n]
        ext = struct.unpack_from('<I', rec, 2)[0]
        ln = struct.unpack_from('<I', rec, 10)[0]
        flags = rec[25]
        nl = rec[32]
        name = rec[33:33 + nl]
        i += n
        if name in (b'\x00', b'\x01'):
            continue
        name = name.decode('ascii', 'replace').split(';')[0]
        full = path + '/' + name
        if flags & 2:
            out.append((full + '/', ext, ln, True))
            read_dir(f, ext, ln, full, out)
        else:
            out.append((full, ext, ln, False))


def main():
    with open(sys.argv[1], 'rb') as f:
        f.seek(16 * 2048)
        pvd = f.read(2048)
        assert pvd[1:6] == b'CD001', 'not ISO9660'
        vol = pvd[40:72].decode('ascii', 'replace').strip()
        blocks = struct.unpack_from('<I', pvd, 80)[0]
        root = pvd[156:156 + 34]
        ext = struct.unpack_from('<I', root, 2)[0]
        ln = struct.unpack_from('<I', root, 10)[0]
        out = []
        read_dir(f, ext, ln, '', out)
    print(f'volume "{vol}", {blocks} blocks ({blocks * 2048} bytes)')
    for p, e, l, d in sorted(out, key=lambda r: r[1]):
        print(f'{e:8d} {e * 2048:#011x} {l:11d} {"<dir>" if d else ""} {p}')


main()

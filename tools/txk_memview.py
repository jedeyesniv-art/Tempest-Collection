"""Show guest words around an address in several TxK memory dumps (txk.exe --memdump), side by side.

  python tools/txk_memview.py <addr hex> <before bytes> <after bytes> d1.bin d2.bin ...  [--changed]
Each line: address, then per dump the word as hex and as float. --changed shows only words that differ between dumps.
"""
import struct
import sys

sys.path.insert(0, __file__.rsplit('\\', 1)[0].rsplit('/', 1)[0])
from txk_memsearch import load  # noqa: E402


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    changed = '--changed' in sys.argv
    addr, before, after = int(args[0], 16), int(args[1]), int(args[2])
    dumps = [load(p) for p in args[3:]]
    for a in range((addr - before) & ~3, addr + after, 4):
        row = []
        for d in dumps:
            w = None
            for base, buf in d.items():
                if base <= a < base + len(buf) - 3:
                    w = struct.unpack_from('<I', buf, a - base)[0]
                    break
            row.append(w)
        if changed and len(set(row)) <= 1:
            continue
        cells = []
        for w in row:
            if w is None:
                cells.append('-' * 22)
            else:
                f = struct.unpack('<f', struct.pack('<I', w))[0]
                cells.append(f'{w:08X} {f:>12.5g}')
        print(f'{a:08X}{"*" if a == addr else " "} ' + '  '.join(cells))


if __name__ == '__main__':
    main()

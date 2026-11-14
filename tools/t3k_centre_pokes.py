"""--poke arguments that move Tempest 3000's hard-coded screen centre (180, 120) in the renderer module library and
the render-job templates to (cx, cy) - a coverage test for the high-resolution work.

python -I tools/t3k_centre_pokes.py cx cy field
"""
import glob
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from t3k_patchimm import encode  # noqa: E402

# (library address, kind, axis): 'add' = add #nn, Sk (10-bit immediate); 'mv' = the 32-bit extension word of
# mv_s #nnnn (16.16 value >> 5).
SITES = [
    (0x806AD7BE, 'add', 'x'), (0x806AD7C2, 'add', 'y'), (0x806AD7C6, 'add', 'x'), (0x806AD7CA, 'add', 'y'),
    (0x806AD7CE, 'add', 'x'), (0x806AD7D2, 'add', 'y'), (0x806AD7D6, 'add', 'x'), (0x806AD7DA, 'add', 'y'),
    (0x806A8E4A, 'add', 'x'), (0x806A8E4E, 'add', 'y'),
    (0x806AEF0A, 'add', 'x'), (0x806AEF0E, 'add', 'y'),
    (0x806A73AE, 'mv', 'y'),
    (0x806AAA18, 'mv', 'y'), (0x806AAA22, 'mv', 'x'), (0x806AAAB6, 'mv', 'y'), (0x806AAAC0, 'mv', 'x'),
    (0x806AE5E2, 'mv', 'x'), (0x806AE5EA, 'mv', 'y'), (0x806AE90A, 'mv', 'x'), (0x806AE912, 'mv', 'y'),
    (0x806AEB76, 'mv', 'x'), (0x806AEB7E, 'mv', 'y'),
]
TEMPLATES_B478 = [0x800B3010, 0x800B3050, 0x800B3090, 0x800B4924, 0x800B4BDC, 0x800B6E70, 0x800B77F0, 0x800B84C4,
                  0x800B9E30, 0x800BA05C, 0x800BAB90, 0x800BAB94]
TEMPLATES_B498 = [0x800B33A0, 0x800B56E0, 0x800B5870, 0x800B5A20, 0x800B8B10, 0x800B9180, 0x800B9280, 0x800B9610,
                  0x800B96E0, 0x800B9CF0, 0x800B9F70]


def library_word(addr):
    """The original 32-bit word at a library address, from the catalogue's module bytes."""
    for f in glob.glob(os.path.join(ROOT, 'aot_catalog', 'm*.bin')):
        src = int(os.path.basename(f)[1:9], 16)
        b = open(f, 'rb').read()
        if src <= addr and addr + 4 <= src + len(b):
            return int.from_bytes(b[addr - src:addr - src + 4], 'big')
    raise SystemExit('no catalogue module covers %08X' % addr)


def main():
    cx, cy, field = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    out = []
    for addr, kind, axis in SITES:
        v = cx if axis == 'x' else cy
        if kind == 'add':
            new = encode(library_word(addr), v)
        else:
            new = 0x88000000 | ((v << 16) >> 5)
        out.append('--poke=%d:%08X:%08X:1' % (field, addr, new))
    for a in TEMPLATES_B478:
        out.append('--poke=%d:%08X:%04X%04X:1' % (field, a, cx, cy))
    for a in TEMPLATES_B498:
        out.append('--poke=%d:%08X:%04X%04X:1' % (field, a, cx, cy + 32))
    print(' '.join(out))


if __name__ == '__main__':
    main()

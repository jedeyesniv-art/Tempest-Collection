"""Generate the code patch table for Tempest 3000 at 720x480 (third_party/NuanceResurrection/src/hires_table.inc).

python -I tools/t3k_hires_table.py > third_party/NuanceResurrection/src/hires_table.inc
Each row: library address, original word, high-res word. The originals come from the module catalogue
(aot_catalog/*.bin), so a site that does not hold the expected word is never touched.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from t3k_centre_pokes import SITES, library_word  # noqa: E402
from t3k_patchimm import decode, encode  # noqa: E402

# mv_s #imm extension words holding a pixel-DMA flags value with the 360-pixel line width (bits 16-23 = width / 8)
DMA_FLAG_SITES = [
    0x806A32A2, 0x806A3352, 0x806A33EE, 0x806A34C2, 0x806A36A4, 0x806A36C4, 0x806A3876, 0x806A38BA, 0x806A4792,
    0x806A4984, 0x806A67D4, 0x806A6856, 0x806A7514, 0x806A7596, 0x806A9070, 0x806A90F2, 0x806AF60A, 0x806AF62A,
    0x806B02C0, 0x806B0344, 0x806B0F82, 0x806B0F8A,
]


def bits_width(width):
    """bits #w, >>#s, Sk: the width is the low 5 bits of the last byte."""
    def f(w):
        assert (w >> 28) == 9 and (w >> 8) & 0xFF == 0xB8, hex(w)
        return (w & ~0x1F) | width
    return f


# The background clear (806A3108) keeps only 8 bits of the tile row (bits #7): rows past 255 wrapped to the top.
def asr_pair(old, new):
    """Two 16-bit 'asr #n, Sk' instructions (low 5 bits = shift) in one word."""
    def f(w):
        for half in (w >> 16, w & 0xFFFF):
            assert half >> 9 == 0x34 >> 1 and half & 0x1F == old, hex(w)
        return w & ~0x001F001F | (new << 16) | new
    return f


def mv_imm(old, new):
    """The extension word of mv_s #imm (low 5 bits of both values zero)."""
    def f(w):
        assert w == mv_word(old), (hex(w), hex(mv_word(old)))
        return mv_word(new)
    return f


# 3D projection: screen scale 0.75 (mv_s #$C000 before mul >>#16) -> 1.5, and the centres (180.5, 120.5) of the
# points / particles (806A5FB0) and objects (806A8990) -> (360.5, 240.5).
SCALE_SITES = [0x806A625C, 0x806A63FC, 0x806A8BBE, 0x806A8DFA]
# 806AE210 projects the 3D-placed objects (claw, enemies) into their entries: position = pos * r6 >> 19 + centre,
# size = word 4 * r6 >> 19. Word 4 is already doubled on read, so only the position multiplies get >> 18.
OBJ_POS_MULS = [0x806AE554, 0x806AE558, 0x806AE5D8, 0x806AE5DC, 0x806AE902, 0x806AE906, 0x806AEAFC, 0x806AEB00,
                0x806AEEB2, 0x806AEEB6, 0x806AEECA, 0x806AEECE]
# (Not the 2D modules' own mv_s #$C000 at 806AA354, 806AC8C8/928, 806ACE86, 806AE546/5C0/8F4/AEE/E86: tried, the text
# went half size and the web surface doubled twice.)
CENTRE_X5 = [0x806A629E, 0x806A6446, 0x806A667E, 0x806A8C0C, 0x806B0176]
CENTRE_Y5 = [0x806A62A4, 0x806A644C, 0x806A6684, 0x806A8C14, 0x806A9382, 0x806B017E]

def imm10(old, new):
    """The 10-bit immediate of a 32-bit ALU instruction (add / cmp #nn, Sk)."""
    def f(w):
        assert decode(w)[0] == old, (hex(w), decode(w))
        return encode(w, new)
    return f


def mul_shift(old, new):
    """mul Si, Sk, >>#n, Sk (32-bit ALU form): the shift is the low 5 bits of the last byte."""
    def f(w):
        assert (w >> 28) == 9 and (w & 0x1F) == old and (w & 0xE0) == 0x80, hex(w)
        return (w & ~0x1F) | new
    return f


OTHER = [(a, mul_shift(19, 18), 'object position projection: mul >>#19 -> >>#18') for a in OBJ_POS_MULS] + [
    # Objects (806A8990), perspective path: (x - 1.0) * (1/z) >> 20 -> >> 19 (x2).
    (0x806A9372, mul_shift(20, 19), 'object projection: mul >>#20 -> >>#19 (x)'),
    (0x806A9376, mul_shift(20, 19), 'object projection: mul >>#20 -> >>#19 (y)'),
    # Title screen tile effect (806B0B30): right / bottom edge tests 354 / 234 -> 714 / 474.
    (0x806B0CA2, imm10(354, 714), 'title tiles: cmp #354 -> #714'),
    (0x806B0CC2, imm10(234, 474), 'title tiles: cmp #234 -> #474'),
    # Objects (806A8990): 16.16 edge values 240.0 / 352.0 -> 480.0 / 704.0.
    (0x806A94B4, mv_imm(0x00F00000, 0x01E00000), 'object edge 240.0 -> 480.0'),
    (0x806A94DC, mv_imm(0x00F00000, 0x01E00000), 'object edge 240.0 -> 480.0'),
    (0x806A9508, mv_imm(0x00F00000, 0x01E00000), 'object edge 240.0 -> 480.0'),
    (0x806A94CE, mv_imm(0x01600000, 0x02C00000), 'object edge 352.0 -> 704.0'),
    (0x806A94F4, mv_imm(0x01600000, 0x02C00000), 'object edge 352.0 -> 704.0'),
    (0x806A937A, mv_imm(0x00B08000, 0x01608000), 'object centre x 176.5 -> 352.5'),
] + [(a, mv_imm(0xC000, 0x18000), 'projection scale 0.75 -> 1.5') for a in SCALE_SITES] + [
    (a, mv_imm(0x00B48000, 0x01688000), 'centre x 180.5 -> 360.5') for a in CENTRE_X5] + [
    (a, mv_imm(0x00788000, 0x00F08000), 'centre y 120.5 -> 240.5') for a in CENTRE_Y5] + [
    (0x806A3412, bits_width(8), 'clear tile row: bits #7 -> #8 (9 bits, rows up to 511)'),
    # Web surface (806AD2B0): packed vertices in half pixels from the centre, unpacked with asr #17 -> asr #16 (x2).
    (0x806AD6F8, asr_pair(17, 16), 'surface vertex unpack: asr #17 -> #16 (r8, r9)'),
    (0x806AD700, asr_pair(17, 16), 'surface vertex unpack: asr #17 -> #16 (r10, r11)'),
    (0x806AD708, asr_pair(17, 16), 'surface vertex unpack: asr #17 -> #16 (r4, r5)'),
    (0x806AD710, asr_pair(17, 16), 'surface vertex unpack: asr #17 -> #16 (r6, r7)'),
]


def mv_word(v):
    assert v & 31 == 0, 'low 5 bits live in the instruction word'
    return 0x88000000 | (v >> 5)


def main():
    rows = []
    for addr, kind, axis in SITES:
        w = library_word(addr)
        old, new = (180, 360) if axis == 'x' else (120, 240)
        if kind == 'add':
            assert decode(w)[0] == old, hex(addr)
            rows.append((addr, w, encode(w, new), 'add #%d -> #%d' % (old, new)))
        else:
            assert w == mv_word(old << 16), hex(addr)
            rows.append((addr, w, mv_word(new << 16), 'mv_s #%d.0 -> #%d.0' % (old, new)))
    for addr in DMA_FLAG_SITES:
        w = library_word(addr)
        v = (w & 0x07FFFFFF) << 5
        assert (v >> 16) & 0xFF == 0x2D, hex(addr)
        nv = (v & ~0xFF0000) | (0x5A << 16)
        rows.append((addr, w, mv_word(nv), 'dma flags %08X -> %08X' % (v, nv)))
    # Other code: (address, original, high-res, comment).
    for addr, new, comment in OTHER:
        w = library_word(addr)
        rows.append((addr, w, new(w), comment))
    print('// Generated by tools/t3k_hires_table.py: {library address, original word, high-res word}.')
    for a, o, n, c in rows:
        print('{0x%08XU, 0x%08XU, 0x%08XU},   // %s' % (a, o, n, c))


if __name__ == '__main__':
    main()

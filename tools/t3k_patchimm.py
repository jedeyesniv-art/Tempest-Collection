"""Re-encode the immediate of Nuon 32-bit 'add #nn, Sk' instructions (ALU32, form 0x02: byte3 >> 5 == 2).

python -I tools/t3k_patchimm.py <hexword> <newimm>   -> the new 32-bit word
Encoding: imm (10 bits) = (byte3 & 0x1F) << 5 | ((byte0 & 3) << 3 | byte1 >> 5); register = byte1 & 0x1F.
"""
import sys


def decode(w):
    b = w.to_bytes(4, 'big')
    return ((b[3] & 0x1F) << 5) | (((b[0] & 3) << 3) | (b[1] >> 5)), b[1] & 0x1F


def encode(w, imm):
    assert 0 <= imm < 1024
    b = bytearray(w.to_bytes(4, 'big'))
    assert b[3] >> 5 == 2, 'not an add #nn, Sk'
    lo, hi = imm & 31, imm >> 5
    b[0] = (b[0] & ~3) | (lo >> 3)
    b[1] = (b[1] & 0x1F) | ((lo & 7) << 5)
    b[3] = (b[3] & ~0x1F) | hi
    return int.from_bytes(b, 'big')


if __name__ == '__main__':
    w = int(sys.argv[1], 16)
    old, reg = decode(w)
    new = encode(w, int(sys.argv[2], 0))
    assert decode(new) == (int(sys.argv[2], 0), reg)
    print('%08X' % new, '(add #%d, r%d -> add #%d, r%d)' % (old, reg, int(sys.argv[2], 0), reg))

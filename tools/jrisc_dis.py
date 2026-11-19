"""Jaguar RISC (GPU) disassembler for a block of memory.

usage: python -I tools/jrisc_dis.py <ram.bin> <src hex> <len hex> <load address hex>
  ram.bin: a main-RAM dump (D2K_RAMDUMP); the code at <src> runs at <load address> (a GPU overlay).
Instruction words are big-endian: opcode 6 bits, reg1 (source / immediate) 5, reg2 (destination / condition) 5.
"""
import sys

OPS = ['add', 'addc', 'addq', 'addqt', 'sub', 'subc', 'subq', 'subqt', 'neg', 'and', 'or', 'xor', 'not', 'btst',
       'bset', 'bclr', 'mult', 'imult', 'imultn', 'resmac', 'imacn', 'div', 'abs', 'sh', 'shlq', 'shrq', 'sha',
       'sharq', 'ror', 'rorq', 'cmp', 'cmpq', 'sat8', 'sat16', 'move', 'moveq', 'moveta', 'movefa', 'movei', 'loadb',
       'loadw', 'load', 'loadp', 'load_r14i', 'load_r15i', 'storeb', 'storew', 'store', 'storep', 'store_r14i',
       'store_r15i', 'move_pc', 'jump', 'jr', 'mmult', 'mtoi', 'normi', 'nop', 'load_r14ri', 'load_r15ri',
       'store_r14ri', 'store_r15ri', 'sat24', 'pack']
IMM = {'addq', 'addqt', 'subq', 'subqt', 'btst', 'bset', 'bclr', 'shlq', 'shrq', 'sharq', 'rorq', 'moveq'}
CONDS = {0: 't', 1: 'ne', 2: 'eq', 4: 'cc', 5: 'hi', 6: 'eq_cc', 8: 'cs', 9: 'ne_cs', 10: 'eq_cs', 20: 'pl',
         21: 'ne_pl', 22: 'eq_pl', 24: 'mi', 25: 'ne_mi', 26: 'eq_mi', 31: 'never'}
ZERO32 = [32] + list(range(1, 32))


def dis(mem, src, n, base):
    out = []
    i = 0
    while i < n:
        w = mem[src + i] << 8 | mem[src + i + 1]
        op, r1, r2 = w >> 10, (w >> 5) & 31, w & 31
        name = OPS[op]
        pc = base + i
        size = 2
        if name == 'movei':
            lo = mem[src + i + 2] << 8 | mem[src + i + 3]
            hi = mem[src + i + 4] << 8 | mem[src + i + 5]
            txt = 'movei #$%08X, r%d' % (hi << 16 | lo, r2)
            size = 6
        elif name in ('jr',):
            off = r1 - 32 if r1 & 16 else r1
            txt = 'jr %s, $%06X' % (CONDS.get(r2, str(r2)), pc + 2 + off * 2)
        elif name == 'jump':
            txt = 'jump %s, (r%d)' % (CONDS.get(r2, str(r2)), r1)
        elif name in ('addq', 'subq', 'addqt', 'subqt', 'shlq', 'shrq', 'sharq', 'rorq'):
            v = ZERO32[r1] if name in ('addq', 'subq', 'addqt', 'subqt') else (32 - r1 if name in ('shlq',) else ZERO32[r1])
            if name == 'shlq': v = 32 - r1
            txt = '%s #%d, r%d' % (name, v, r2)
        elif name == 'cmpq':
            v = r1 - 32 if r1 & 16 else r1
            txt = 'cmpq #%d, r%d' % (v, r2)
        elif name in IMM:
            txt = '%s #%d, r%d' % (name, r1, r2)
        elif name in ('load_r14i', 'load_r15i'):
            txt = 'load (r%s+%d), r%d' % (name[6:8], ZERO32[r1] * 4, r2)
        elif name in ('store_r14i', 'store_r15i'):
            txt = 'store r%d, (r%s+%d)' % (r2, name[7:9], ZERO32[r1] * 4)
        elif name in ('load_r14ri', 'load_r15ri'):
            txt = 'load (r%s+r%d), r%d' % (name[6:8], r1, r2)
        elif name in ('store_r14ri', 'store_r15ri'):
            txt = 'store r%d, (r%s+r%d)' % (r2, name[7:9], r1)
        elif name.startswith('load'):
            txt = '%s (r%d), r%d' % (name, r1, r2)
        elif name.startswith('store'):
            txt = '%s r%d, (r%d)' % (name, r2, r1)
        elif name in ('nop',):
            txt = 'nop'
        elif name in ('neg', 'not', 'abs', 'sat8', 'sat16', 'sat24', 'resmac', 'mmult', 'mtoi', 'normi'):
            txt = '%s r%d' % (name, r2) if name not in ('mmult', 'mtoi', 'normi') else '%s r%d, r%d' % (name, r1, r2)
        elif name == 'move_pc':
            txt = 'move pc, r%d' % r2
        elif name == 'pack':
            txt = '%s r%d' % ('unpack' if r1 else 'pack', r2)
        else:
            txt = '%s r%d, r%d' % (name, r1, r2)
        out.append('%06X  %s' % (pc, txt))
        i += size
    return out


if __name__ == '__main__':
    mem = open(sys.argv[1], 'rb').read()
    src, n, base = (int(a, 16) for a in sys.argv[2:5])
    print('\n'.join(dis(mem, src, n, base)))

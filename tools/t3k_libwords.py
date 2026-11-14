"""Print raw library words (the module bytes from aot_catalog/*.bin) next to disassembly lines.

python -I tools/t3k_libwords.py <asm file> <regex>
The asm file is a module disassembly as written for logs/t3k/m*.asm ("localpc [libaddr]: text"); each line matching
the regex is printed with the 32-bit word and the following 16-bit word at its library address.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load():
    mods = []
    for f in glob.glob(os.path.join(ROOT, 'aot_catalog', 'm*.bin')):
        mods.append((int(os.path.basename(f)[1:9], 16), open(f, 'rb').read()))
    return mods


def word(mods, a, n=6):
    for s, b in mods:
        if s <= a and a + n <= s + len(b):
            return b[a - s:a - s + n].hex(' ')
    return '?'


def main():
    mods = load()
    pat = re.compile(sys.argv[2])
    for line in open(sys.argv[1]):
        m = re.match(r'\s*([0-9A-F]+) \[([0-9A-F]+)\]:(.*)', line)
        if m and pat.search(m.group(3)):
            print(m.group(2), word(mods, int(m.group(2), 16)), '|', m.group(3).strip())


if __name__ == '__main__':
    main()

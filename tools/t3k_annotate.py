"""Annotate a Tempest 3000 renderer module's disassembly (t3k --disasm output) for reading.

python -I tools/t3k_annotate.py <disasm file> [--local 203002D0] > annotated.txt

Each line gets the module-local pc (what NUANCE_PCLOG takes): drawing modules load at local $203002D0 (MPE1's pre-pass
modules at $20300000: --local 20300000). Comments name:
  - entry words: $20100030 + 4k = w<k> (the display list entry MPE0 is drawing), $20100000 = the job word;
    $20100018 / $2010001C = the band's clip rectangle (x lo/hi, y lo/hi halves)
  - the standard helpers every module starts with (identical prologue, checked on 806A3F50 / 806AD2B0 / 806AF1E8):
    +$04 / +$0A wait for main-bus DMA, +$24 wait for both DMA channels, +$54 linear DMA local -> bus (write: r0 words,
    r1 bus address, r2 local), +$A4 linear DMA bus -> local (read: r0 words from r1 to r2), +$F6 / +$108 issue a
    main-bus / other-bus DMA command (v0 at $201000B0)
  - local jump / call targets as library addresses
"""
import re
import sys

HELPERS = {0x04: 'wait main-bus DMA', 0x0A: 'wait main-bus DMA', 0x24: 'wait both DMA channels',
           0x42: 'wait both DMA channels', 0x54: 'DMA write: r0 words, local r2 -> bus r1',
           0xA4: 'DMA read: r0 words, bus r1 -> local r2', 0xF6: 'main-bus DMA command (v0)',
           0x108: 'other-bus DMA command (v0)'}
LOCAL_NAMES = {0x20100000: 'job word', 0x20100018: 'band clip x', 0x2010001C: 'band clip y'}


def main():
    path = sys.argv[1]
    local = 0x203002D0
    if '--local' in sys.argv:
        local = int(sys.argv[sys.argv.index('--local') + 1], 16)
    base = None
    out = []
    for line in open(path):
        line = line.rstrip('\n')
        m = re.match(r'--- mpe\d ([0-9A-F]{8})', line)
        if m:
            base = int(m.group(1), 16)
            out.append(line + '   (local pc = lib - %08X + %08X)' % (base, local))
            continue
        m = re.match(r'([0-9A-F]{8}): (.*)', line)
        if not m or base is None:
            out.append(line)
            continue
        lib = int(m.group(1), 16)
        text = m.group(2)
        notes = []
        for a in re.findall(r'\$([0-9A-F]{8})', text):
            v = int(a, 16)
            if 0x20100030 <= v <= 0x2010006C and (v - 0x20100030) % 4 == 0:
                notes.append('$%s = w%d' % (a[-3:], (v - 0x20100030) // 4))
            elif v in LOCAL_NAMES:
                notes.append(LOCAL_NAMES[v])
            elif local <= v < local + 0x2000:
                off = v - local
                name = HELPERS.get(off)
                notes.append('-> %08X%s' % (base + off, ' ' + name if name else ''))
        brackets = re.sub(r'\s*\[[^\]]*\]', '', text)   # (the disassembler's [delay slot, target] pairs)
        out.append('%08X  L%08X  %-60s%s' % (lib, lib - base + local, brackets,
                                             ('  ; ' + '; '.join(dict.fromkeys(notes))) if notes else ''))
    print('\n'.join(out))


if __name__ == '__main__':
    main()

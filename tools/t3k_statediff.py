"""Compare two Tempest 3000 save states (core llamasoft-nuon retro_serialize) by section.

python -I tools/t3k_statediff.py a.state b.state

Prints the differing bytes per section (RAM main / system bus, environment, MPE0-3 by field, video, media, syscall,
timers) with addresses: MPE local memory as $20000000 + offset, RAM as bus addresses.
"""
import struct
import sys

TAGS = [(0x4E554F4E, 'header'), (0x52414D30, 'ram'), (0x454E5630, 'env'), (0x4D504530, 'mpe0'), (0x4D504531, 'mpe1'),
        (0x4D504532, 'mpe2'), (0x4D504533, 'mpe3'), (0x56494430, 'video'), (0x4D454430, 'media'), (0x53595330, 'syscall'),
        (0x54494D30, 'timers'), (0x454E4421, 'end')]
MAIN = 16 << 20
SYS = 8 << 20
CTL = ['mpectl', 'excepsrc', 'excepclr', 'excephalten', 'pcfetch', 'pcroute', 'pcexec', 'intvec1', 'intvec2', 'intsrc',
       'intclr', 'intctl', 'inten1', 'inten1set', 'inten1clr', 'inten2sel', 'xybase', 'uvbase', 'linpixctl', 'clutbase',
       'sp', 'dabreak', 'odmactl', 'odmacptr', 'mdmactl', 'mdmacptr', 'comminfo', 'commctl', 'dcachectl', 'icachectl',
       'vdmactla', 'vdmactlb', 'vdmaptra', 'vdmaptrb', 'pcfetchnext', 'ecuSkipCounter', 'overlayMask',
       'interpretNextPacket', 'ba_reg_offset', 'ba_control', 'ba_mipped_xoffset']
TIMERS = ['emu_cycles', 'next_timer0', 'next_timer1', 'next_audio']


def sections(d):
    pos = []
    at = 8
    for t, name in TAGS:
        k = d.find(struct.pack('<I', t), at)
        pos.append((k, name))
        if k >= 0:
            at = k + 4
    return pos


def where(name, off):
    """A readable location for byte off (from the section's tag)."""
    o = off - 4
    if name == 'ram':
        if o < MAIN:
            return 'main $%08X' % (0x40000000 + o)
        if o < MAIN + SYS:
            return 'sys $%08X' % (0x80000000 + o - MAIN)
        return 'eeprom / heap +%d' % (o - MAIN - SYS)
    if name.startswith('mpe'):
        if o < 192:
            return 'r%d' % (o // 4) if o < 128 else 'reg_union[%d]' % (o // 4)
        if o < 384:
            return 'tempreg[%d]' % ((o - 192) // 4)
        o -= 384
        if o < 4 * len(CTL):
            return CTL[o // 4]
        o -= 4 * len(CTL)
        if o < 32:
            return 'commxmit/recv[%d]' % (o // 4)
        o -= 32
        if o < 8:
            return 'cycleCounter'
        if o < 10:
            return 'flags'
        return 'local $%08X' % (0x20000000 + o - 10)
    if name == 'timers':
        return TIMERS[o // 8] if o < 32 else 'parked[%d]' % (o - 32)
    return '+%d' % o


def main():
    a = open(sys.argv[1], 'rb').read()
    b = open(sys.argv[2], 'rb').read()
    sa, sb = sections(a), sections(b)
    if [k for k, _ in sa] != [k for k, _ in sb]:
        print('layouts differ:', sa, sb)
    n = min(len(a), len(b))
    diffs = []
    step = 1 << 16
    for i in range(0, n, step):
        if a[i:i + step] != b[i:i + step]:
            diffs.extend(j for j in range(i, min(i + step, n)) if a[j] != b[j])
    print('%d differing bytes' % len(diffs))
    groups = {}
    for j in diffs:
        name, start = 'header', 0
        for k, nm in sa:
            if 0 <= k <= j:
                name, start = nm, k
        loc = where(name, j - start)
        if loc.startswith('tempreg') and '--all' not in sys.argv:
            continue   # the interpreter's scratch copies
        gran = 0xFFFF if name == 'ram' else 15   # RAM by 64 KB, local memory by 16 bytes
        key = (name, loc.rsplit('$', 1)[0] + ('$%08X' % (int(loc.rsplit('$', 1)[1], 16) & ~gran) if '$' in loc else ''))
        groups.setdefault(key, []).append(j)
    for (name, loc), js in sorted(groups.items(), key=lambda kv: kv[1][0]):
        print('  %-8s %-28s %3d bytes' % (name, loc, len(js)))


if __name__ == '__main__':
    main()

"""Three-way Tempest 3000 save state compare: bytes equal in A and C but different in B (a setting toggled on in B
and back off in C).

python -I tools/t3k_state3.py a.state b.state c.state [max lines]
Locations as tools/t3k_statediff.py names them; framebuffer-looking main RAM is listed by count only.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import t3k_statediff as sd  # noqa: E402


def main():
    paths = sys.argv[1:4]
    limit = int(sys.argv[4]) if len(sys.argv) > 4 else 200
    raw = [open(p, 'rb').read() for p in paths]
    a, b, c = (np.frombuffer(r, np.uint8) for r in raw)
    assert len(a) == len(b) == len(c), 'state sizes differ'
    hits = np.nonzero((a == c) & (a != b))[0]
    secs = [(k, n) for k, n in sd.sections(raw[0]) if k >= 0]
    print('%d bytes' % len(hits))
    shown = 0
    main_count = 0
    for off in hits:
        name, start = None, 0
        for k, n in secs:
            if k <= off:
                name, start = n, k
        loc = sd.where(name, int(off - start))
        if loc.startswith('main'):
            main_count += 1
            continue
        if shown < limit:
            print('%-28s %02X %02X' % (loc, a[off], b[off]))
            shown += 1
    print('main RAM bytes (not listed): %d' % main_count)


if __name__ == '__main__':
    main()

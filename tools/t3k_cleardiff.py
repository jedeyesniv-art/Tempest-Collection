"""Re-render test, part 1 (frame buffer dependence): compare a plain run's screenshots (dir A) with a run where every
frame buffer was filled with a marker colour just before the renderers started drawing into it (dir B,
NUANCE_CLEARTARGET). A changed pixel depends on what the buffer held before (feedback, read-modify-write blending,
areas the picture never draws).

python -I tools/t3k_cleardiff.py <dirA> <dirB> <out.png> [cols]
Prints per shot: field, % pixels changed, % exactly the marker (never drawn this picture); writes a contact sheet:
each cell = plain picture, changed pixels tinted red.
"""
import os
import re
import sys

import numpy as np
from PIL import Image


def load(p):
    return np.asarray(Image.open(p).convert('RGB')).astype(np.int16)


def main():
    da, db, out = sys.argv[1:4]
    cols = int(sys.argv[4]) if len(sys.argv) > 4 else 12
    names = sorted((n for n in os.listdir(da) if n.endswith('.bmp')), key=lambda n: int(re.findall(r'\d+', n)[0]))
    cells = []
    marker = None
    for n in names:
        if not os.path.exists(os.path.join(db, n)):
            continue
        a, b = load(os.path.join(da, n)), load(os.path.join(db, n))
        diff = np.any(np.abs(a - b) > 2, axis=2)
        if marker is None and diff.any():
            # the marker colour = the most common colour among changed pixels of the first shot with changes
            vals, counts = np.unique(b[diff].reshape(-1, 3), axis=0, return_counts=True)
            marker = vals[counts.argmax()]
        exact = np.all(np.abs(b - marker) <= 2, axis=2) & diff if marker is not None else diff & False
        f = int(re.findall(r'\d+', n)[0])
        print('f%5d changed %6.2f%%  marker %6.2f%%' % (f, 100.0 * diff.mean(), 100.0 * exact.mean()))
        c = a.copy()
        c[diff] = (c[diff] * 0.3 + np.array([255, 0, 0]) * 0.7)
        c[exact] = (0, 255, 255)
        cells.append(Image.fromarray(c.clip(0, 255).astype(np.uint8)).resize((180, 120)))
    if marker is not None:
        print('marker colour (as shown):', marker)
    rows = (len(cells) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * 182, rows * 122), (40, 40, 40))
    for i, c in enumerate(cells):
        sheet.paste(c, ((i % cols) * 182, (i // cols) * 122))
    sheet.save(out)


if __name__ == '__main__':
    main()

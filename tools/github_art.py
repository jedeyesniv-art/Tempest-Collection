"""GitHub page art for the Tempest Collection, made from the same ingredients as tools/steam_art.py (the launcher's
covers, glowing edges, the arcade TEMPEST lettering, the 5x7 pixel font).

python -I tools/github_art.py [out dir]      (default art\\github\\)

  header_1600x640.png   README header: title, tagline, the six covers in the launcher's carousel
  social_1280x640.png   the repository's social preview (Settings > Social preview)
"""
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import steam_art as sa  # noqa: E402

GOLD = (255, 200, 40)


def carousel(img, cx, cy, base_h, step):
    """The six covers, the middle two largest, far ones dimmed, with reflections (as the hero)."""
    n = len(sa.ORDER)
    slots = []
    for i, gid in enumerate(sa.ORDER):
        d = abs(i - (n - 1) / 2)
        sc = 1 - (d - 0.5) * 0.14
        ch = int(base_h * sc)
        cw = int(ch * 0.75)
        x = cx + (i - (n - 1) / 2) * step
        slots.append((d, gid, int(x - cw / 2), int(cy - ch / 2), cw, ch))
    for d, gid, x, y, cw, ch in sorted(slots, reverse=True):
        sa.reflection(img, gid, x, y, cw, ch, frac=0.22)
        sa.framed(img, gid, x, y, cw, ch, glow=1.0 - (d - 0.5) * 0.2)
        dim = Image.new('RGBA', (cw, ch), (0, 0, 0, int(255 * min(0.45, (d - 0.5) * 0.2))))
        img.alpha_composite(dim, (x, y))


def banner(w, h, unit, tag_unit, base_h, step):
    img = sa.backdrop(w, h, sa.ORDER, dark=0.5)
    bottom = sa.title(img, w // 2, int(h * 0.05), unit)
    sa.glow_text(img, 'SIX GAMES  -  ONE LAUNCHER  -  ON PC', w // 2, bottom + tag_unit * 7, tag_unit, GOLD, 0.8)
    top = bottom + tag_unit * 24
    carousel(img, w // 2, top + base_h // 2, base_h, step)
    return img.convert('RGB')


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(sa.ROOT, 'art', 'github')
    os.makedirs(out, exist_ok=True)
    banner(1600, 640, 11, 3, 300, 245).save(os.path.join(out, 'header_1600x640.png'))
    banner(1280, 640, 10, 3, 250, 205).save(os.path.join(out, 'social_1280x640.png'))
    print('->', out)


if __name__ == '__main__':
    main()

"""Steam library art for the Tempest Collection shortcut (TempestCollection.exe added to Steam as a non-Steam game),
made from the launcher's ingredients: the six covers (art\\panels\\, tools/launcher_art.py), glowing panel edges in
each game's colour and the launcher's glowing 5x7 pixel lettering (read from src/pixfont.cpp).

python -I tools/steam_art.py [out dir]      (default art\\steam\\ - not in git: the covers are the publishers')

  capsule_600x900.png      library grid (portrait; the Deck's main view)
  wide_capsule_920x430.png the "recent games" row
  hero_1920x620.png        the banner at the top of the game's page (no text: Steam puts the logo over it)
  logo.png                 transparent, over the hero
  icon_256.png             the claw (res/t2k.ico)
"""
import math
import os
import re
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
PANELS = os.path.join(ROOT, 'art', 'panels')
ORDER = ['t81', 't2k', 't3k', 'sg', 'txk', 't4k']
ACCENT = {'t81': (90, 200, 255), 't2k': (255, 200, 40), 't3k': (255, 80, 255), 'sg': (120, 255, 120),
          'txk': (80, 200, 255), 't4k': (80, 120, 255)}
MAGENTA, CYAN = (255, 80, 255), (120, 200, 255)


def glyphs():
    src = ''.join(open(os.path.join(ROOT, 'src', f), encoding='utf-8').read() for f in ('pixfont.cpp', 'launcher_view.cpp'))
    g = {}
    for c, rows in re.findall(r"\{'(\\?.)', \"([.#]+)\"\}", src):
        g[c.replace('\\', '') or c] = rows.ljust(35, '.')[:35]
    return g


GLYPHS = glyphs()


def text_mask(s, unit):
    """The pixel text as an L mask (the launcher's 5x7 font, 6 units a letter)."""
    w = max(1, (len(s) * 6 - 1) * unit)
    m = Image.new('L', (w, 7 * unit), 0)
    d = ImageDraw.Draw(m)
    for i, ch in enumerate(s):
        rows = GLYPHS.get(ch)
        if not rows:
            continue
        for r in range(7):
            for k in range(5):
                if rows[r * 5 + k] == '#':
                    x, y = (i * 6 + k) * unit, r * unit
                    d.rectangle([x, y, x + int(unit * 0.92) - 1, y + int(unit * 0.92) - 1], fill=255)
    return m


def glow_text(img, s, cx, y, unit, col, glow=1.0, pad_blur=None):
    """Draws glowing pixel text centred at cx onto an RGBA image (the launcher's look: soft halo, pale core)."""
    m = text_mask(s, unit)
    pad = unit * 6
    big = Image.new('L', (m.width + 2 * pad, m.height + 2 * pad), 0)
    big.paste(m, (pad, pad))
    x0, y0 = int(cx - m.width / 2) - pad, int(y) - pad
    halo = big.filter(ImageFilter.GaussianBlur(pad_blur or unit * 1.6))
    halo = halo.point(lambda v: min(255, int(v * 1.6 * glow)))
    layer = Image.new('RGBA', big.size, col + (0,))
    layer.putalpha(halo)
    img.alpha_composite(layer, (x0, y0))
    core = tuple(min(255, int(c * 0.6 + 110)) for c in col)
    layer = Image.new('RGBA', big.size, core + (0,))
    layer.putalpha(big)
    img.alpha_composite(layer, (x0, y0))


def cover(gid, w, h):
    return Image.open(os.path.join(PANELS, gid + '.png')).convert('RGBA').resize((w, h), Image.LANCZOS)


def framed(img, gid, x, y, w, h, edge=None, glow=1.0):
    """A cover with the launcher's glowing edge in its game's colour."""
    col = ACCENT[gid]
    e = edge or max(2, w // 90)
    halo = Image.new('L', (w + 8 * e, h + 8 * e), 0)
    ImageDraw.Draw(halo).rectangle([4 * e, 4 * e, 4 * e + w - 1, 4 * e + h - 1], outline=255, width=e * 2)
    halo = halo.filter(ImageFilter.GaussianBlur(e * 2.2)).point(lambda v: min(255, int(v * 1.8 * glow)))
    layer = Image.new('RGBA', halo.size, col + (0,))
    layer.putalpha(halo)
    img.alpha_composite(layer, (x - 4 * e, y - 4 * e))
    img.alpha_composite(cover(gid, w, h), (x, y))
    ImageDraw.Draw(img).rectangle([x, y, x + w - 1, y + h - 1], outline=tuple(min(255, c // 2 + 128) for c in col) + (255,),
                                  width=max(1, e // 2))


def backdrop(w, h, gids, dark=0.55):
    """Blurred covers side by side, darkened, with a vignette: the launcher's backdrop."""
    bg = Image.new('RGBA', (w, h), (0, 0, 0, 255))
    n = len(gids)
    for i, gid in enumerate(gids):
        c = cover(gid, max(8, w // n + 2), h).filter(ImageFilter.GaussianBlur(max(w, h) / 40))
        bg.alpha_composite(c, (i * w // n, 0))
    bg = bg.filter(ImageFilter.GaussianBlur(max(w, h) / 60))
    black = Image.new('RGBA', (w, h), (0, 0, 0, int(255 * dark)))
    bg.alpha_composite(black)
    # vignette
    v = Image.new('L', (w, h), 0)
    px = v.load()
    for yy in range(0, h, 2):
        for xx in range(0, w, 2):
            d = math.hypot((xx - w / 2) / (w / 2), (yy - h / 2) / (h / 2)) / math.sqrt(2)
            a = int(255 * 0.85 * min(1, max(0, (d - 0.35) / 0.65)))
            for dy in (0, 1):
                for dx in (0, 1):
                    if xx + dx < w and yy + dy < h:
                        px[xx + dx, yy + dy] = a
    vl = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    vl.putalpha(v)
    bg.alpha_composite(vl)
    return bg


def reflection(img, gid, x, y, w, h, frac=0.3, alpha=0.22):
    c = cover(gid, w, h).transpose(Image.FLIP_TOP_BOTTOM).crop((0, 0, w, int(h * frac)))
    grad = Image.linear_gradient('L').resize((w, int(h * frac))).point(lambda v: int((255 - v) * alpha))
    c.putalpha(ImageChops.multiply(c.getchannel('A'), grad))
    img.alpha_composite(c, (x, y + h + max(2, w // 90)))


ARCADE_LOGO = os.path.join(ROOT, 'art', 't81 logo new.png')   # the arcade's own TEMPEST lettering (owner)


def arcade_logo(width):
    """The arcade TEMPEST lettering (429 px line art) at `width`: upscaled, edges re-sharpened."""
    im = Image.open(ARCADE_LOGO).convert('RGBA')
    h = round(im.height * width / im.width)
    rgb = im.convert('RGB').resize((width, h), Image.LANCZOS)
    k = width / im.width
    a = im.getchannel('A').resize((width, h), Image.BICUBIC)
    if k > 1.5:
        a = a.point(lambda v: max(0, min(255, int((v - 128) * 2.2 + 128))))
    out = rgb.convert('RGBA')
    out.putalpha(a)
    return out


def title(img, cx, y, unit, glow=1.0):
    """The arcade 'TEMPEST' (white, a magenta glow), 'COLLECTION' under it in cyan pixel letters; returns the bottom.
    (Sized as the pixel 'TEMPEST' it replaced: 41 font units wide.)"""
    lg = arcade_logo(int(41 * unit * 1.05))
    x0 = int(cx - lg.width / 2)
    pad = unit * 4
    halo = Image.new('L', (lg.width + 2 * pad, lg.height + 2 * pad), 0)
    halo.paste(lg.getchannel('A'), (pad, pad))
    halo = halo.filter(ImageFilter.GaussianBlur(unit * 1.4)).point(lambda v: min(255, int(v * 1.5 * glow)))
    layer = Image.new('RGBA', halo.size, MAGENTA + (0,))
    layer.putalpha(halo)
    img.alpha_composite(layer, (x0 - pad, int(y) - pad))
    img.alpha_composite(lg, (x0, int(y)))
    cy = int(y) + lg.height + unit * 2
    glow_text(img, 'COLLECTION', cx, cy, max(1, int(unit * 0.62)), CYAN, glow)
    return cy + int(unit * 0.62) * 7


def capsule(out):
    w, h = 600, 900
    img = backdrop(w, h, ['t2k', 't3k', 'txk'])
    bottom = title(img, w // 2, 56, 12)
    # the six covers, 3 x 2, under the title
    cw, ch, gap = 168, 224, 22
    x0 = (w - 3 * cw - 2 * gap) // 2
    y0 = bottom + 60
    for i, gid in enumerate(ORDER):
        framed(img, gid, x0 + (i % 3) * (cw + gap), y0 + (i // 3) * (ch + gap), cw, ch)
    img.convert('RGB').save(os.path.join(out, 'capsule_600x900.png'))


def wide(out):
    w, h = 920, 430
    img = backdrop(w, h, ORDER)
    bottom = title(img, w // 2, 26, 9)
    cw, ch, gap = 128, 171, 14
    x0 = (w - 6 * cw - 5 * gap) // 2
    y0 = bottom + 34
    for i, gid in enumerate(ORDER):
        framed(img, gid, x0 + i * (cw + gap), y0, cw, ch)
    img.convert('RGB').save(os.path.join(out, 'wide_capsule_920x430.png'))


def hero(out):
    """The launcher's carousel as a banner: six covers, the middle two largest, reflections; no text."""
    w, h = 1920, 620
    img = backdrop(w, h, ORDER, dark=0.45)
    # sizes fall off from the middle (as the launcher's panels), drawn far ones first
    slots = []
    n = len(ORDER)
    for i, gid in enumerate(ORDER):
        d = abs(i - (n - 1) / 2)          # 0.5 .. 2.5
        sc = 1 - (d - 0.5) * 0.14
        ch = int(400 * sc)
        cw = int(ch * 0.75)
        cx = w / 2 + (i - (n - 1) / 2) * 340
        slots.append((d, gid, int(cx - cw / 2), int(250 - ch / 2 + 40), cw, ch))
    for d, gid, x, y, cw, ch in sorted(slots, reverse=True):
        reflection(img, gid, x, y, cw, ch)
        framed(img, gid, x, y, cw, ch, glow=1.0 - (d - 0.5) * 0.2)
        dim = Image.new('RGBA', (cw, ch), (0, 0, 0, int(255 * min(0.45, (d - 0.5) * 0.2))))
        img.alpha_composite(dim, (x, y))
    img.convert('RGB').save(os.path.join(out, 'hero_1920x620.png'))


def logo(out):
    w, h = 1280, 560
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    title(img, w // 2, 70, 26, glow=1.1)
    bbox = img.getchannel('A').point(lambda v: 255 if v > 4 else 0).getbbox()
    img.crop(bbox).save(os.path.join(out, 'logo.png'))


def icon(out):
    im = Image.open(os.path.join(ROOT, 'res', 't2k.ico'))
    im.size = (256, 256)
    im.load()
    im.convert('RGBA').save(os.path.join(out, 'icon_256.png'))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'art', 'steam')
    os.makedirs(out, exist_ok=True)
    missing = [g for g in ORDER if not os.path.exists(os.path.join(PANELS, g + '.png'))]
    if missing:
        sys.exit('no panels for %s: run tools/launcher_art.py first' % ', '.join(missing))
    for f in (capsule, wide, hero, logo, icon):
        f(out)
        print('made', f.__name__)
    print('->', out)


if __name__ == '__main__':
    main()

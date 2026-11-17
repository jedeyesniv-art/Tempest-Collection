"""Launcher panel art: the owner's cover art and title logos (art\\, any format Pillow reads: webp, avif, jpg, png,
no extension) made into the launcher's portrait panels (3:4) and trimmed logos, saved as PNG in art\\panels\\
(not in git: the art is the publishers', never published; art\\ is in .gitignore).

python -I tools/launcher_art.py [--sheet out.png]

  art\\panels\\<id>.png       the game's cover (the first in COVERS), 1200 x 1600
  art\\panels\\<id>_<n>.png   alternative covers (n = 2, 3 ..: the mockup's C key cycles them)
  art\\panels\\<id>_logo.png  the title logo, transparent borders trimmed

Per cover: the source file and the crop's centre (0..1 of the source's width / height; covers that are already
portrait are only trimmed to 3:4). --sheet writes a contact sheet of the panels and logos for checking.
"""
import os
import sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
ART = os.path.join(ROOT, 'art')
OUT = os.path.join(ART, 'panels')
PANEL_W, PANEL_H = 1200, 1600   # 3:4

# id: [(source file in art\, crop centre x, crop centre y), alternatives ...]
COVERS = {
    't81': [('t81 better cover.png', 0.5, 0.5), ('t81 better cover 2.png', 0.5, 0.5),
            ('tempest-1981-v0-y9qhcchq1xqa1.webp', 0.5, 0.5)],
    't2k': [('tempest 2k big.jpg', 0.5, 0.5)],
    't3k': [('Tempest-3000-Cover-Art.png', 0.46, 0.5)],
    'sg':  [('sapce giraffe.0252ce74-a89d-4e1a-86ad-b993d456b56b', 0.5, 0.5)],
    'txk': [('txb22.png', 0.5, 0.5)],
    't4k': [('Tempest 4k_Cover.webp', 0.5, 0.5)],
}
LOGOS = {
    't81': 't81 logo new.png',   # the arcade's own lettering (owner), 429 px: upscaled (UPSCALE)
    't2k': 't2l logo.png', 't3k': 't3k logo.png',
    'sg': 'sg logo.png', 'txk': 'txk logo.png', 't4k': 't4k logo.png',
}


def panel(src, cx, cy):
    im = Image.open(src).convert('RGB')
    w, h = im.size
    target = PANEL_W / PANEL_H
    if w / h > target:   # too wide: crop the width around cx
        cw, ch = round(h * target), h
    else:                # too tall: crop the height around cy
        cw, ch = w, round(w / target)
    x0 = min(max(0, round(cx * w - cw / 2)), w - cw)
    y0 = min(max(0, round(cy * h - ch / 2)), h - ch)
    return im.crop((x0, y0, x0 + cw, y0 + ch)).resize((PANEL_W, PANEL_H), Image.LANCZOS), (w, h), (x0, y0, cw, ch)


UPSCALE = {'t81'}   # small flat line art: 4x, edges re-sharpened so it stays crisp at 4K (not soft glows like TxK's)


def logo(src, upscale=False):
    im = Image.open(src).convert('RGBA')
    if upscale:
        k = 4
        rgb = im.convert('RGB').resize((im.width * k, im.height * k), Image.LANCZOS)
        a = im.getchannel('A').resize(rgb.size, Image.BICUBIC).point(lambda v: max(0, min(255, int((v - 128) * 2.2 + 128))))
        im = rgb.convert('RGBA')
        im.putalpha(a)
    # trim nearly transparent borders (soft glows fade out to a few alpha levels)
    box = im.getchannel('A').point(lambda a: 255 if a > 6 else 0).getbbox()
    return im.crop(box) if box else im


def main():
    os.makedirs(OUT, exist_ok=True)
    made = []
    for gid, covers in COVERS.items():
        for n, (name, cx, cy) in enumerate(covers):
            src = os.path.join(ART, name)
            if not os.path.exists(src):
                print('%-4s missing: %s' % (gid, name))
                continue
            im, size, crop = panel(src, cx, cy)
            out = gid + ('.png' if n == 0 else '_%d.png' % (n + 1))
            im.save(os.path.join(OUT, out), optimize=True)
            made.append(im)
            print('%-10s %s %dx%d -> crop %s' % (out, name, size[0], size[1], crop))
    logos = []
    for gid, name in LOGOS.items():
        src = os.path.join(ART, name)
        if not os.path.exists(src):
            print('%-4s logo missing: %s' % (gid, name))
            continue
        im = logo(src, gid in UPSCALE)
        im.save(os.path.join(OUT, gid + '_logo.png'), optimize=True)
        logos.append(im)
        print('%-10s %s -> %dx%d' % (gid + '_logo.png', name, im.size[0], im.size[1]))
    if '--sheet' in sys.argv:
        out = sys.argv[sys.argv.index('--sheet') + 1]
        tw, th = 180, 240
        sheet = Image.new('RGB', (max(tw * len(made), 3 * 400), th + 2 * 140), (40, 40, 50))
        for i, im in enumerate(made):
            sheet.paste(im.resize((tw, th), Image.LANCZOS), (i * tw, 0))
        for i, im in enumerate(logos):
            t = im.copy()
            t.thumbnail((380, 130))
            sheet.paste(t, ((i % 3) * 400 + 10, th + (i // 3) * 140 + 5), t)
        sheet.save(out)


if __name__ == '__main__':
    main()

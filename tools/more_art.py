"""The launcher's MORE LLAMASOFT panel art: each game's wide store picture and a cover made of them all.

python -I tools/more_art.py [--download] [--sheet out.png]

  --download   fetch the Steam games' store headers (460 x 215, public store images) into art\\more_src\\
               (<appid>_header.jpg; files already there are kept)

Inputs: art\\more_src\\<appid>_header.jpg for the Steam games; our two other collections' wide Steam art from their
repos (C:\\Gridios\\Launcher\\assets\\steam\\wide capsule.png, C:\\LlamatronRecomp\\res\\steam\\wide.png), copied
into art\\more_src\\ the first time (so later runs don't need those repos); Frogacuda's OpenMinotaur: its title screen
(owner's capture, 2026-11-27) cropped to 920 x 430 below the FPS overlay as art\\more_src\\openminotaur_wide.png.
Outputs (art\\ is not in git):
  art\\panels\\more.png          the panel's cover (3:4, 1200 x 1600): the wide pictures as a wall
  art\\panels\\more_<key>.png    each game's wide picture (the list rows; key = the launcher's MoreItem key)
"""
import os
import shutil
import sys
import urllib.request

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SRC = os.path.join(ROOT, 'art', 'more_src')
OUT = os.path.join(ROOT, 'art', 'panels')
PANEL_W, PANEL_H = 1200, 1600

# key, Steam app (None = ours), source file in art\more_src, where ours come from - in the launcher's list order
ITEMS = [
    ('gridrunner_revolution', '27810', None),
    ('minotaur_arcade', '906110', None),
    ('polybius', '906120', None),
    ('moose_life', '1342740', None),
    ('akka_arrh', '2005680', None),
    ('jeff_minter_story', '2236680', None),
    ('i_robot', '2932910', None),
    ('minotaur_project', None, r'C:\Gridios\Launcher\assets\steam\wide capsule.png'),
    ('openminotaur', None, None),   # Frogacuda's: art\more_src\openminotaur_wide.png, cropped from his title screen
    ('llamasoft_st', None, r'C:\LlamatronRecomp\res\steam\wide.png'),
]


def src_path(key, app):
    return os.path.join(SRC, f'{app}_header.jpg' if app else f'{key}_wide.png')


def download():
    os.makedirs(SRC, exist_ok=True)
    for key, app, _ in ITEMS:
        if not app or os.path.exists(src_path(key, app)):
            continue
        url = f'https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/{app}/header.jpg'
        print('fetch', url)
        with urllib.request.urlopen(url, timeout=30) as r:
            data = r.read()
        with open(src_path(key, app), 'wb') as f:
            f.write(data)


def main():
    if '--download' in sys.argv:
        download()
    os.makedirs(OUT, exist_ok=True)
    wides = []
    for key, app, ours in ITEMS:
        p = src_path(key, app)
        if not os.path.exists(p) and ours and os.path.exists(ours):
            shutil.copyfile(ours, p)
        if not os.path.exists(p):
            print('missing', p, '(run with --download, or check the other repos)')
            continue
        im = Image.open(p).convert('RGB')
        # one size for every row picture: 920 x 430 (Steam's wide capsule; the 460 x 215 headers are doubled)
        im = im.resize((920, 430), Image.LANCZOS)
        im.save(os.path.join(OUT, f'more_{key}.png'))
        wides.append(im)
    # the cover: a dark panel, the pictures in two staggered columns (the second half a row lower)
    cover = Image.new('RGB', (PANEL_W, PANEL_H), (8, 6, 14))
    gap = 28
    cw = (PANEL_W - 3 * gap) // 2
    ch = round(cw * 430 / 920)
    for i, im in enumerate(wides):
        col, row = i % 2, i // 2
        x = gap + col * (cw + gap)
        y = 70 + row * (ch + gap) + (ch // 2 if col else 0)
        if y >= PANEL_H:   # (the last ones run into the bottom fade)
            continue
        cover.paste(im.resize((cw, ch), Image.LANCZOS), (x, y))
    # a soft fade at the bottom (the launcher's reflection reads the bottom third)
    fade = Image.new('L', (1, PANEL_H))
    for y in range(PANEL_H):
        fade.putpixel((0, y), int(255 * max(0.0, (y - PANEL_H * 0.82) / (PANEL_H * 0.18)) ** 1.5))
    cover = Image.composite(Image.new('RGB', cover.size, (8, 6, 14)), cover, fade.resize(cover.size))
    cover.save(os.path.join(OUT, 'more.png'))
    print(f'{len(wides)} pictures -> {OUT}\\more*.png')
    if '--sheet' in sys.argv:
        out = sys.argv[sys.argv.index('--sheet') + 1]
        sheet = Image.new('RGB', (PANEL_W // 2 + 20 + 460, PANEL_H // 2), (0, 0, 0))
        sheet.paste(cover.resize((PANEL_W // 2, PANEL_H // 2)), (0, 0))
        for i, im in enumerate(wides):
            sheet.paste(im.resize((184, 86)), (PANEL_W // 2 + 20, i * 88))
        sheet.save(out)


if __name__ == '__main__':
    main()

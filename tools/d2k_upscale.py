"""Upscale Defender 2000 pictures (tools/d2k_art_sheet.py's <out>_pics/<n>.png) with Real-ESRGAN (ncnn Vulkan build in
third_party/realesrgan), keeping their see-through parts.

  python -I tools/d2k_upscale.py <pics dir> <out dir> [--model realesrgan-x4plus] [--only 2,16,21] [--compare file.png]

Per picture: colours spread into the see-through pixels (so the upscaler sees no black fringe), an 8 px edge-copied
border (tiny sprites), RGB upscaled 4x by the model, the opacity mask upscaled by the same model as grey and cut at
50%; result = <out>/<n>.png (RGBA, 4x). --compare writes original (4x blocky) | upscaled rows on a checker.
"""
import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, 'third_party', 'realesrgan', 'realesrgan-ncnn-vulkan.exe')
PAD = 8


def bleed(rgba):
    """Fill see-through pixels with the mean of their nearest opaque neighbours (repeated dilation)."""
    rgb = rgba[..., :3].astype(np.float32)
    known = rgba[..., 3] > 0
    if not known.any():
        return rgba[..., :3].copy()
    rgb[~known] = 0
    for _ in range(64):
        if known.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape, np.float32)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)):
            k = np.roll(np.roll(known, dy, 0), dx, 1)
            c = np.roll(np.roll(rgb, dy, 0), dx, 1)
            acc += c * k[..., None]
            cnt += k
        new = ~known & (cnt > 0)
        rgb[new] = acc[new] / cnt[new][:, None]
        known = known | new
    return np.clip(rgb + 0.5, 0, 255).astype(np.uint8)


def run_model(src_dir, dst_dir, model):
    subprocess.run([EXE, '-i', src_dir, '-o', dst_dir, '-n', model, '-s', '4', '-f', 'png'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=os.path.dirname(EXE))


def checker(w, h, s=8):
    yy, xx = np.mgrid[0:h, 0:w]
    c = np.where(((yy // s) + (xx // s)) % 2 == 0, 40, 56).astype(np.uint8)
    return Image.fromarray(np.dstack([c, c, c + 8, np.full_like(c, 255)]), 'RGBA')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pics')
    ap.add_argument('out')
    ap.add_argument('--model', default='realesrgan-x4plus')
    ap.add_argument('--only', default='')
    ap.add_argument('--compare', default='')
    a = ap.parse_args()
    names = sorted((f for f in os.listdir(a.pics) if f.endswith('.png')), key=lambda f: int(f[:-4]))
    if a.only:
        keep = set(a.only.split(','))
        names = [f for f in names if f[:-4] in keep]
    os.makedirs(a.out, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        src, dst = os.path.join(tmp, 'in'), os.path.join(tmp, 'out')
        os.makedirs(src)
        os.makedirs(dst)
        for f in names:
            im = np.array(Image.open(os.path.join(a.pics, f)).convert('RGBA'))
            rgb = np.pad(bleed(im), ((PAD, PAD), (PAD, PAD), (0, 0)), mode='edge')
            alpha = np.pad(im[..., 3], PAD, mode='constant')
            Image.fromarray(rgb, 'RGB').save(os.path.join(src, 'c_' + f))
            Image.fromarray(np.dstack([alpha] * 3), 'RGB').save(os.path.join(src, 'a_' + f))
        run_model(src, dst, a.model)
        rows = []
        for f in names:
            rgb = np.array(Image.open(os.path.join(dst, 'c_' + f)).convert('RGB'))
            al = np.array(Image.open(os.path.join(dst, 'a_' + f)).convert('L'))
            p = 4 * PAD
            rgb, al = rgb[p:-p, p:-p], al[p:-p, p:-p]
            al = np.where(al >= 128, 255, 0).astype(np.uint8)
            out = Image.fromarray(np.dstack([rgb, al]), 'RGBA')
            out.save(os.path.join(a.out, f))
            rows.append((f, out))
    print(f'{len(names)} pictures -> {a.out} ({a.model})')
    if a.compare:
        gap = 12
        tiles = []
        for f, up in rows:
            orig = Image.open(os.path.join(a.pics, f)).convert('RGBA')
            big = orig.resize((orig.width * 4, orig.height * 4), Image.NEAREST)
            w, h = big.width * 2 + gap, big.height
            t = Image.new('RGBA', (w, h), (16, 16, 20, 255))
            for i, im in enumerate((big, up)):
                bg = checker(im.width, im.height)
                bg.alpha_composite(im)
                t.paste(bg, (i * (big.width + gap), 0))
            tiles.append(t)
        width = max(t.width for t in tiles)
        sheet = Image.new('RGB', (width, sum(t.height + gap for t in tiles)), (16, 16, 20))
        y = 0
        for t in tiles:
            sheet.paste(t.convert('RGB'), (0, y))
            y += t.height + gap
        sheet.save(a.compare)
        print(a.compare)


if __name__ == '__main__':
    sys.exit(main())

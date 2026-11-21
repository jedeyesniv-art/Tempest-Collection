"""Build Defender 2000's high-res art pack from an art census (tools/d2k_art_sheet.py <dumps> --out X).

  python -I tools/d2k_art_pack.py X_pics [--out build/d2k_art] [--model realesrgan-x4plus] [--skip-upscale]

Pack = <out>/pack.txt (the census's index: each picture's place in memory) + src/<n>.png (the original, 1x: d2k.exe
draws the replacement only where the sprite's pixels equal it) + hi/<n>.png (4x, tools/d2k_upscale.py; backdrop ridges smoothed by tools/d2k_ridges.py).
Left out: drawing buffers (radar, HUD, text, Classic's playfield), Classic-only pictures (arcade pixel art: kept
blocky) and tiny pictures (both sides <= --small, default 24: humanoids, score pop-ups, shots). The upscaler invents
their 1-2 px details anew in every animation frame (a humanoid's face white in one frame, a black blob in the next),
so animated they strobe; drawn from the original they stay steady. The pack is made from the cartridge's art: like the ROM, never in git, never published.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pics')
    ap.add_argument('--out', default='build/d2k_art')
    ap.add_argument('--model', default='realesrgan-x4plus')
    ap.add_argument('--skip-upscale', action='store_true', help='reuse <out>/hi')
    ap.add_argument('--small', type=int, default=24, help='pictures with both sides <= this stay original')
    ap.add_argument('--ridge-detail', type=float, default=0.5, help='rocky roughness on smoothed ridges (1x pixels)')
    a = ap.parse_args()
    keep, lines = [], []
    with open(os.path.join(a.pics, 'pack.txt')) as f:
        for line in f:
            if line.startswith('#'):
                lines.append(line)
                continue
            v = line.split()
            if v[8] == '1' or v[9] == 'classic' or (int(v[6]) <= a.small and int(v[7]) <= a.small):
                continue
            keep.append(v[0])
            lines.append(line)
    os.makedirs(os.path.join(a.out, 'src'), exist_ok=True)
    os.makedirs(os.path.join(a.out, 'hi'), exist_ok=True)
    for n in keep:
        shutil.copyfile(os.path.join(a.pics, n + '.png'), os.path.join(a.out, 'src', n + '.png'))
    if not a.skip_upscale:
        subprocess.run([sys.executable, '-I', os.path.join(HERE, 'd2k_upscale.py'), a.pics, os.path.join(a.out, 'hi'),
                        '--model', a.model, '--only', ','.join(keep)], check=True)
        # The backdrops' stair-stepped ridges -> smooth outlines (tools/d2k_ridges.py).
        subprocess.run([sys.executable, '-I', os.path.join(HERE, 'd2k_ridges.py'), os.path.join(a.out, 'src'),
                        os.path.join(a.out, 'hi'), '--detail', str(a.ridge_detail)], check=True)
    for sub in ('src', 'hi'):   # pictures no longer in the pack (an earlier build's)
        for fn in os.listdir(os.path.join(a.out, sub)):
            if fn.endswith('.png') and fn[:-4] not in keep:
                os.remove(os.path.join(a.out, sub, fn))
    with open(os.path.join(a.out, 'pack.txt'), 'w') as f:
        f.writelines(lines)
    print(f'{len(keep)} pictures -> {a.out}')


if __name__ == '__main__':
    sys.exit(main())

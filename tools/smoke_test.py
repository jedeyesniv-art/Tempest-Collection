"""One-command smoke test before packaging: every program starts, loads its game and draws something.

  python -I tools/smoke_test.py [--dist] [--only t2k,t3k,...] [--no-windows]

Checks (each runs with its own --userdata under logs/smoke/, never the owner's saves):
  t2k  headless boot -> level select -> play (the known script), the picture at field 2290 (remaster off:
       headless, the remaster's own drawing isn't in the picture - only the HUD would be)
  t3k  native: --check in a small window (boot -> play -> name entry), picture 900 in play
  t81  headless attract, rev 3 and Tempest Tubes (setting version)
  txk  headless boot to the title
  launcher  one frame (--shot)
  menus     each game's menu in a window with every section open (--menu=all --winshot), unless --no-windows
  --dist    the play folder: README names the T3K disc file and keeps the T4K launch option literal
A picture passes when enough of it is lit (per check: a game's play picture 10 %, so a HUD alone fails). Results: a table, and logs/smoke/sheet.png (every picture).
"""
import argparse
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'logs', 'smoke')
B = os.path.join(ROOT, 'build')


def fwd(p):
    return p.replace('\\', '/')


def lit(path, min_lit=0.01):
    """Fraction of pixels brighter than near-black, or None when the picture is missing."""
    try:
        from PIL import Image
        im = Image.open(path).convert('L')
    except Exception:
        return None
    hist = im.histogram()
    return sum(hist[24:]) / max(1, sum(hist))


def run(name, args, pictures, expect=(), timeout=900, min_lit=0.03):
    """Runs one program; returns (name, ok, note, pictures). min_lit: the share of its pictures that must be lit
    (play pictures and the launcher are 20 %+, menus 4-6 %, t81's thin vectors under 2 %, a HUD alone ~1 %)."""
    log = os.path.join(OUT, name + '.txt')
    for p in pictures:   # a picture left from an earlier run must never pass for this one
        if os.path.exists(p):
            os.remove(p)
    t0 = time.time()
    try:
        with open(log, 'w') as f:
            r = subprocess.run(args, stdout=f, stderr=subprocess.STDOUT, cwd=ROOT, timeout=timeout)
        code = r.returncode
    except subprocess.TimeoutExpired:
        return name, False, f'timed out after {timeout} s', pictures
    secs = time.time() - t0
    text = open(log, errors='replace').read()
    notes = []
    ok = code == 0
    if code != 0:
        notes.append(f'exit {code}')
    for e in expect:
        if e not in text:
            ok = False
            notes.append(f'no "{e}" in its output')
    for p in pictures:
        f = lit(p)
        if f is None:
            ok = False
            notes.append(f'no picture {os.path.basename(p)}')
        elif f < min_lit:
            ok = False
            notes.append(f'{os.path.basename(p)} nearly black ({f:.1%} lit, needs {min_lit:.0%})')
    if ok:
        notes.append(f'{secs:.0f} s')
    return name, ok, ', '.join(notes), pictures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dist', action='store_true', help='also check the play folder (dist/TempestCollection)')
    ap.add_argument('--only', default='', help='comma list of t2k, t3k, t81, txk, launcher, menus')
    ap.add_argument('--no-windows', action='store_true', help='skip the window tests (menus)')
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    want = set(a.only.split(',')) if a.only else {'t2k', 't3k', 't81', 'txk', 'launcher', 'menus'}
    if a.no_windows:
        want.discard('menus')
    o = lambda n: fwd(os.path.join(OUT, n))
    ud = lambda n: '--userdata=' + o('ud_' + n)
    jag = os.path.join(ROOT, 'Tempest 2000 (1994).jag')

    headless = []
    if 't2k' in want:
        headless.append(('t2k play', [os.path.join(B, 't2k.exe'), jag, '--headless', '--frames=2300', ud('t2k'),
                         '--set=remaster=0', '--press=400:b', '--press=600:b', '--press=800:b', '--press=1000:b', '--press=1500:b',
                         '--shot=2290:' + o('t2k_play.bmp')], [o('t2k_play.bmp')], ['loaded'], 900, 0.10))
    if 't3k' in want:
        # native T3K (2026-11-25): no headless mode; --check plays a scripted game in a small window (boot -> level
        # select -> play). Since Frogacuda's stable checkpoint (2026-11-26, 120 Hz scene timing) it plays on until
        # --fine-check-limit gameplay updates (~3,860 pictures at the 60 Hz default and gameplay 22, 2026-11-27);
        # picture 3700 is in play on level 1.
        headless.append(('t3k play', [os.path.join(B, 't3k.exe'), '--check', '--fine-check-limit=600',
                         '--winsize=960x720', ud('t3k'), '--winshot=3700:' + o('t3k_play.bmp')], [o('t3k_play.bmp')],
                         ['Native selected level', 'Native bounded soak'], 300, 0.08))
        # (8 %: T3K's play picture is mostly black space - a normal level-1 picture read 9.8 % against 10 %)
    if 't81' in want:
        for v in ('tempest', 'temptube'):
            headless.append((f't81 {v}', [os.path.join(B, 't81.exe'), '--headless', '--frames=610', ud('t81_' + v),
                             '--set=version=' + v, '--shot=600:' + o(f't81_{v}.bmp')], [o(f't81_{v}.bmp')],
                             [f'version {v};'], 900, 0.01))
    if 'txk' in want:
        headless.append(('txk title', [os.path.join(B, 'txk.exe'), '--headless', '--frames=1900', ud('txk'),
                         '--shot=1890:' + o('txk_title.bmp')], [o('txk_title.bmp')], []))
    if 'launcher' in want:
        headless.append(('launcher', [os.path.join(B, 'tempest.exe'), '--shot=' + o('launcher.bmp')],
                         [o('launcher.bmp')], [], 300, 0.10))

    results = []
    with ThreadPoolExecutor(max_workers=3) as ex:
        for r in ex.map(lambda t: run(*t), headless):
            results.append(r)
            print(f'{"PASS" if r[1] else "FAIL"}  {r[0]:16s} {r[2]}', flush=True)

    if 'menus' in want:   # windows, one at a time (each takes the screen for a moment)
        menus = [('t2k menu', [os.path.join(B, 't2k.exe'), jag]), ('t3k menu', [os.path.join(B, 't3k.exe')]),
                 ('t81 menu', [os.path.join(B, 't81.exe')]), ('txk menu', [os.path.join(B, 'txk.exe')])]
        for name, cmd in menus:
            g = name.split()[0]
            pic = o(f'{g}_menu.bmp')
            size = [] if g == 'txk' else ['--winsize=1280x800']
            r = run(name, cmd + size + [ud(g + '_menu'), '--menu=all', '--winshot=40:' + pic], [pic], [], timeout=300)
            results.append(r)
            print(f'{"PASS" if r[1] else "FAIL"}  {r[0]:16s} {r[2]}', flush=True)

    if a.dist:
        readme = os.path.join(ROOT, 'dist', 'TempestCollection', 'README.txt')
        ok, notes = True, []
        try:
            t = open(readme, errors='replace').read()
            if 'game\\Tempest 3000.' not in t:
                ok = False
                notes.append('README does not name the T3K disc')
            if '$t3kDisc' in t:
                ok = False
                notes.append('README has an unexpanded $t3kDisc')
            if 'WINEDLLOVERRIDES' in t and '${@' not in t and 'Win7-8' in t:
                ok = False
                notes.append('README lost the T4K launch option\'s ${@...}')
        except OSError:
            ok, notes = False, ['no dist README']
        results.append(('dist README', ok, ', '.join(notes) or 'ok', []))
        print(f'{"PASS" if ok else "FAIL"}  {"dist README":16s} {", ".join(notes) or "ok"}', flush=True)

    # every picture on one sheet
    try:
        from PIL import Image
        pics = [p for r in results for p in r[3] if os.path.exists(p)]
        if pics:
            w, h = 320, 240
            sheet = Image.new('RGB', (w * 4, h * ((len(pics) + 3) // 4)))
            for i, p in enumerate(pics):
                im = Image.open(p).convert('RGB')
                im.thumbnail((w, h))
                sheet.paste(im, ((i % 4) * w, (i // 4) * h))
            sheet.save(os.path.join(OUT, 'sheet.png'))
            print('pictures:', os.path.join(OUT, 'sheet.png'))
    except Exception as e:
        print('no sheet:', e)

    failed = [r[0] for r in results if not r[1]]
    print(f'{len(results) - len(failed)} of {len(results)} passed' + (': FAILED ' + ', '.join(failed) if failed else ''))
    sys.exit(1 if failed else 0)


main()

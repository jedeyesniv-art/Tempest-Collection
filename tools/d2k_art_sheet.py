"""Pictures + contact sheets from Defender 2000 art dumps (d2k.exe with D2K_ART_DUMP=<dir>; see d2k.cpp ArtDump).

  python -I tools/d2k_art_sheet.py <dir> [<dir> ...] [--out logs/d2k/art/sheet] [--scale 2] [--width 1800]

Each dumped rectangle sits in memory as a grid (row = pixel address / line pitch). Rectangles of the same pitch and
depth that overlap there with the same stored values (>= 90% of the shared pixels) are windows into one picture
(a scrolling backdrop's strips) and are joined; the rest (a sprite's animation frames) stay apart. Writes:
  <out>_pics/<n>.png   each picture, RGBA (first colours seen)
  <out>_N.png          small pictures, numbered, on a dark checker (orange number = seen in several colourings)
  <out>_big_N.png      big ones (160 px wide or 120 tall and up)
  <out>_buffers_N.png  a few contents of each place the dump found to be a drawing buffer (not art; to check)
  <out>.txt            n, size, depth, memory place, rectangles joined, uses, frames
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

BPP = [1, 2, 4, 8, 16]


def read_dump(d):
    uses = {}
    with open(os.path.join(d, 'index.txt')) as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            v = line.split()
            uses[int(v[0])] = (int(v[12]), int(v[13]), int(v[10]), int(v[11]))   # uses, colourings, first, last
    buffers = set()   # places the dump found to be drawing buffers: (data, dwidth, depth, first pixel)
    if os.path.exists(os.path.join(d, 'buffers.txt')):
        with open(os.path.join(d, 'buffers.txt')) as f:
            for line in f:
                v = line.split()
                buffers.add((int(v[0], 16), int(v[1]), int(v[2]), int(v[3])))
    recs = []
    n_rec = 0
    with open(os.path.join(d, 'rects.bin'), 'rb') as f:
        buf = f.read()
    at = 0
    while at + 48 <= len(buf):
        hd = struct.unpack_from('<12i', buf, at)
        assert hd[0] == 0x524B3244, 'bad record'
        data, dwidth, depth, skip, w, h, typ, flags, frame, pitch, indexed = hd[1:]
        at += 48
        n = w * h
        if at + 6 * n > len(buf):
            break   # (a dump still being written)
        val = np.frombuffer(buf, np.uint16, n, at).reshape(h, w)
        at += 2 * n
        rgba = np.frombuffer(buf, np.uint8, 4 * n, at).reshape(h, w, 4)
        at += 4 * n
        n_rec += 1
        u = uses.get(n_rec, (1, 1, frame, frame))
        buffer = (data, dwidth, depth, skip) in buffers
        if buffer and sum(r['buffer'] and r['data'] == data for r in recs[-12:]) >= 3:
            continue   # (a few contents of each buffer are enough to see what it is)
        bpp = BPP[depth]
        pitch_px = dwidth * 64 // bpp
        idx = data * 8 // bpp + skip
        recs.append(dict(dir=d, data=data, dwidth=dwidth, depth=depth, w=w, h=h, type=typ, flags=flags,
                         frame=frame, pitch=pitch, buffer=buffer, val=val, rgba=rgba, uses=u[0], colours=u[1], first=u[2],
                         last=u[3], group=(depth, dwidth, pitch), row=idx // max(pitch_px, 1),
                         col=idx % max(pitch_px, 1)))
    print(f'{d}: {n_rec} rectangles, {len(buffers)} drawing buffers')
    return recs


def join(recs):
    parent = list(range(len(recs)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    groups = {}
    for i, r in enumerate(recs):
        groups.setdefault(r['group'], []).append(i)
    for ids in groups.values():
        if len(ids) < 2:
            continue
        r0 = np.array([recs[i]['row'] for i in ids])
        c0 = np.array([recs[i]['col'] for i in ids])
        r1 = r0 + np.array([recs[i]['h'] for i in ids])
        c1 = c0 + np.array([recs[i]['w'] for i in ids])
        for a in range(len(ids)):
            hit = np.nonzero((r0[a + 1:] < r1[a]) & (r1[a + 1:] > r0[a]) & (c0[a + 1:] < c1[a]) &
                             (c1[a + 1:] > c0[a]))[0]
            for b in hit + a + 1:
                i, j = ids[a], ids[b]
                if find(i) == find(j):
                    continue
                A, B = recs[i], recs[j]
                y0, y1 = max(A['row'], B['row']), min(A['row'] + A['h'], B['row'] + B['h'])
                x0, x1 = max(A['col'], B['col']), min(A['col'] + A['w'], B['col'] + B['w'])
                va = A['val'][y0 - A['row']:y1 - A['row'], x0 - A['col']:x1 - A['col']]
                vb = B['val'][y0 - B['row']:y1 - B['row'], x0 - B['col']:x1 - B['col']]
                oa = A['rgba'][y0 - A['row']:y1 - A['row'], x0 - A['col']:x1 - A['col'], 3] > 0
                ob = B['rgba'][y0 - B['row']:y1 - B['row'], x0 - B['col']:x1 - B['col'], 3] > 0
                same = ((va == vb) & (oa == ob)) | (~oa & ~ob)
                if same.mean() >= 0.9:
                    parent[find(i)] = find(j)
    comps = {}
    for i in range(len(recs)):
        comps.setdefault(find(i), []).append(i)
    pics = []
    for ids in comps.values():
        rs = [recs[i] for i in ids]
        r0 = min(r['row'] for r in rs)
        c0 = min(r['col'] for r in rs)
        h = max(r['row'] + r['h'] for r in rs) - r0
        w = max(r['col'] + r['w'] for r in rs) - c0
        img = np.zeros((h, w, 4), np.uint8)
        done = np.zeros((h, w), bool)
        for r in sorted(rs, key=lambda r: r['frame']):   # the first seen wins
            y, x = r['row'] - r0, r['col'] - c0
            free = ~done[y:y + r['h'], x:x + r['w']]
            img[y:y + r['h'], x:x + r['w']][free] = r['rgba'][free]
            done[y:y + r['h'], x:x + r['w']] = True
        first = min(rs, key=lambda r: (r['dir'], r['frame']))
        pics.append(dict(img=img, w=w, h=h, rects=len(rs), uses=sum(r['uses'] for r in rs),
                         colours=max(r['colours'] for r in rs), first=first['frame'], dir=first['dir'],
                         depth=first['depth'], data=min(r['data'] for r in rs), dwidth=first['dwidth'],
                         scaled=any(r['type'] == 1 for r in rs), buffer=any(r['buffer'] for r in rs), cover=float(done.mean())))
    return pics


def checker(w, h, s=4):
    yy, xx = np.mgrid[0:h, 0:w]
    c = np.where(((yy // s) + (xx // s)) % 2 == 0, 40, 52).astype(np.uint8)
    return Image.fromarray(np.dstack([c, c, c + 8, np.full_like(c, 255)]), 'RGBA')


def pages(items, scale, width, prefix, font):
    pad, label_h = 6, 12
    placed, page, x, y, shelf = [], [], pad, pad, 0
    for it in items:
        s = scale if it['w'] * scale <= width - 2 * pad else 1
        w, h = it['w'] * s, it['h'] * s
        cw = max(w, 28)
        if x + cw + pad > width and x > pad:
            x, y, shelf = pad, y + shelf + label_h + pad, 0
        if y + h + label_h + pad > 4000 and page:
            placed.append((page, y + shelf + label_h + pad))
            page, x, y, shelf = [], pad, pad, 0
        page.append((it, x, y, s))
        x += cw + pad
        shelf = max(shelf, h)
    if page:
        placed.append((page, y + shelf + label_h + pad))
    files = []
    for n, (page, ph) in enumerate(placed):
        sheet = Image.new('RGBA', (width, ph), (16, 16, 20, 255))
        dr = ImageDraw.Draw(sheet)
        for it, x, y, s in page:
            im = Image.fromarray(it['img'], 'RGBA')
            if s != 1:
                im = im.resize((im.width * s, im.height * s), Image.NEAREST)
            bg = checker(im.width, im.height)
            bg.alpha_composite(im)
            sheet.paste(bg, (x, y))
            col = (255, 200, 120, 255) if it['colours'] > 1 else (170, 170, 180, 255)
            dr.text((x, y + im.height + 1), str(it['n']), fill=col, font=font)
        path = f'{prefix}_{n + 1}.png'
        sheet.convert('RGB').save(path)
        files.append(path)
    return files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dirs', nargs='+')
    ap.add_argument('--out', default='logs/d2k/art/sheet')
    ap.add_argument('--scale', type=int, default=2)
    ap.add_argument('--width', type=int, default=1800)
    a = ap.parse_args()
    recs = []
    for d in a.dirs:
        recs += read_dump(d)
    pics = join(recs)
    order = {d: i for i, d in enumerate(a.dirs)}
    pics.sort(key=lambda p: (order[p['dir']], p['first']))
    os.makedirs(a.out + '_pics', exist_ok=True)
    for i, p in enumerate(pics):
        p['n'] = i + 1
        Image.fromarray(p['img'], 'RGBA').save(os.path.join(a.out + '_pics', f'{i + 1}.png'))
    font = ImageFont.load_default()
    is_big = lambda p: p['w'] >= 160 or p['h'] >= 120
    art = [p for p in pics if not p['buffer']]
    files = pages([p for p in art if not is_big(p)], a.scale, a.width, a.out, font)
    files += pages([p for p in art if is_big(p)], 1, a.width, a.out + '_big', font)
    files += pages([p for p in pics if p['buffer']], 1, a.width, a.out + '_buffers', font)
    with open(a.out + '.txt', 'w') as f:
        f.write('# n w h depth(4 = 16-bit) data dwidth rects uses colourings scaled buffer cover first dir\n')
        for p in pics:
            f.write(f"{p['n']} {p['w']} {p['h']} {p['depth']} {p['data']:06X} {p['dwidth']} {p['rects']} {p['uses']} "
                    f"{p['colours']} {int(p['scaled'])} {int(p['buffer'])} {p['cover']:.2f} {p['first']} {p['dir']}\n")
    px = sum(p['w'] * p['h'] for p in art)
    big = sum(is_big(p) for p in art)
    print(f'{len(recs)} rectangles -> {len(art)} pictures ({big} big), {px} pixels, '
          f'{sum(p["colours"] > 1 for p in art)} in several colourings; {len(pics) - len(art)} from drawing buffers')
    for p in files:
        print(p)


if __name__ == '__main__':
    sys.exit(main())

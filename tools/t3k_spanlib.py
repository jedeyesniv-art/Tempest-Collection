"""Shared parsing for the Tempest 3000 entry-type investigations (native renderer): import it from a fit script.

Logs (run them with tools/t3k_logrun.py, from a play state):
  span log   NUANCE_SPANLOG=<type> + NUANCE_JOBLOG: 'spanentry w0..w15 p<picture> j<job>' when the entry being drawn
             changes, 'span rd|wr x y len px..' (one row) / 'spanb rd|wr x y len rows px..' (a block): frame buffer
             pixels MPE0 read (the destination before) and wrote (after); NUANCE_PCLOG lines ('pc f.. p.. <pc> r .. e ..')
             interleave in the same file.
  entry log  NUANCE_ENTRYLOG + NUANCE_ENTRYDATA=1: 'g<n> f<field> p<picture> <addr> m<readers> w0..w15 mods .. data
             @addr:w:w..' - each entry with every bus word MPE0 read for it.

Typical fit script:
    import t3k_spanlib as sl
    ents = sl.load_entries(entlog, 0x0010000D)
    def prepare(ev):                     # ev: Entry event; return a model context or None to skip the entry
        words, mem = sl.entry_for(ents, ev)
        ...
    def predict(ctx, x, y, dest):        # dest = (Y, Cr, Cb, Z) bytes; return the predicted (Y, Cr, Cb)
        ...
    sl.fit(spanlog, prepare, predict)    # mean error per channel, the worst pixels
"""
import collections
import math

# --- words and pixels ---


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def hi16(w):
    """Signed high half (x of a position word)."""
    return s16(w >> 16)


def lo16(w):
    """Signed low half (y of a position word)."""
    return s16(w)


def chans(p):
    """A YCrCbZ pixel word -> (Y, Cr, Cb, Z) bytes."""
    return ((p >> 24) & 255, (p >> 16) & 255, (p >> 8) & 255, p & 255)


def as_loaded(p):
    """What an MPE pixel load (ld_p / ld_pz, 32-bit) puts in a vector register, in byte units: Y unsigned, Cr / Cb
    signed about 0 (byte - 128), Z x 4 (it loads as byte << 24, the others << 22)."""
    y, cr, cb, z = chans(p)
    return (float(y), float(cr - 128), float(cb - 128), float(4 * z))


def bilinear16(tex, u, v, size=16):
    """Bilinear sample of a size x size YCrCbZ texture (texel i at coordinate i, wrapping): u, v in texels (float), or
    ints taken as 16.16. Returns 4 floats (bytes)."""
    if isinstance(u, int):
        u = u / 65536.0
    if isinstance(v, int):
        v = v / 65536.0
    iu, iv = math.floor(u), math.floor(v)
    fu, fv = u - iu, v - iv
    m = size - 1

    def t(a, b):
        return chans(tex[(b & m) * size + (a & m)])
    c00, c10, c01, c11 = t(iu, iv), t(iu + 1, iv), t(iu, iv + 1), t(iu + 1, iv + 1)
    out = []
    for k in range(4):
        top = c00[k] + (c10[k] - c00[k]) * fu
        bot = c01[k] + (c11[k] - c01[k]) * fu
        out.append(top + (bot - top) * fv)
    return out


def ycc_to_rgb(y, cr, cb):
    """Nuance's display conversion (BT.601, video range) -> 0..255 RGB floats."""
    def cl(v):
        return min(max(v, 0.0), 1.0)
    yy = cl(y * (255 / 219) / 255 - 16 / 219)
    r_ = cl(cr * (255 / 224) / 255 - 16 / 224) - 0.5
    b_ = cl(cb * (255 / 224) / 255 - 16 / 224) - 0.5
    return (255 * cl(yy + 1.402 * r_), 255 * cl(yy - 0.34413 * b_ - 0.714136 * r_), 255 * cl(yy + 1.772 * b_))


# --- entry log ---


def load_entries(path, type_word=None, readers=1):
    """{picture: [(words, mem), ...]} for entries of type_word (None: all) read by MPE0 (readers mask bit 0);
    mem maps bus addresses to the words MPE0 read."""
    pics = collections.defaultdict(list)
    tw = None if type_word is None else '%08X' % type_word
    for line in open(path):
        tk = line.split()
        if len(tk) < 21 or not tk[0].startswith('g'):
            continue
        if tw and tk[20] != tw:
            continue
        if readers and not int(tk[4][1:], 16) & readers:
            continue
        mem = {}
        if 'data' in tk:
            for chunk in tk[tk.index('data') + 1:]:
                parts = chunk[1:].split(':')
                base = int(parts[0], 16)
                for i, w in enumerate(parts[1:]):
                    mem[base + 4 * i] = int(w, 16)
        pics[int(tk[2][1:])].append(([int(t, 16) for t in tk[5:21]], mem))
    return pics


def read_block(mem, addr, n):
    """n words from an entry's captured memory (0 where MPE0 didn't read)."""
    return [mem.get(addr + 4 * i, 0) for i in range(n)]


def entry_for(ents, ev, match_from=1):
    """The (words, mem) of the logged entry the span log's entry event belongs to: same picture, words equal from
    w<match_from> on (the module may rewrite w0 while drawing, e.g. the position per glyph). None if not found."""
    for words, mem in ents.get(ev.picture, []):
        if words[match_from:] == ev.words[match_from:]:
            return words, mem
    return None


# --- span log ---

Entry = collections.namedtuple('Entry', 'words picture job')
Pixels = collections.namedtuple('Pixels', 'x y dest out')        # one row: lists of pixel words
Pc = collections.namedtuple('Pc', 'field picture pc r e extra')  # registers r[0..31], entry words e[0..15]


def parse_pc(tk):
    # 'pc f<field> p<picture> <pc> r <32 regs> e <16 words> [m.. ] [v..]' (older logs have no p<picture>)
    i = 2
    picture = None
    if tk[2].startswith('p'):
        picture = int(tk[2][1:])
        i = 3
    pc = int(tk[i], 16)
    r = [int(x, 16) for x in tk[i + 2:i + 34]]
    e = [int(x, 16) for x in tk[i + 35:i + 51]]
    return Pc(int(tk[1][1:]), picture, pc, r, e, tk[i + 51:])


def events(path, min_picture=0):
    """Entry / Pixels / Pc events in log order. Pixels pairs each write with the latest read of that row covering it."""
    pend = {}
    cur_pic = 0
    for line in open(path):
        tk = line.split()
        if not tk:
            continue
        k = tk[0]
        if k == 'spanentry':
            cur_pic = int(tk[17][1:])
            if cur_pic >= min_picture:
                yield Entry([int(t, 16) for t in tk[1:17]], cur_pic, int(tk[18][1:], 16) if len(tk) > 18 else 0)
            continue
        if cur_pic < min_picture:
            continue
        if k == 'span' or k == 'spanb':
            rows = 1 if k == 'span' else int(tk[5])
            x, y, n = int(tk[2]), int(tk[3]), int(tk[4])
            first = 5 if k == 'span' else 6
            px = [int(v, 16) for v in tk[first:first + n * rows]]
            for r in range(rows):
                row = px[r * n:(r + 1) * n]
                if tk[1] == 'rd':
                    pend.setdefault(y + r, []).append((x, n, row))
                    del pend[y + r][:-8]
                else:
                    # the latest read of this row covering the written range (modules may read a 32-pixel row and
                    # write shorter pieces of it)
                    for rx, rn, rpx in reversed(pend.get(y + r, [])):
                        if rx <= x and x + n <= rx + rn:
                            yield Pixels(x, y + r, rpx[x - rx:x - rx + n], row)
                            break
        elif k == 'pc':
            yield parse_pc(tk)


# --- the fit loop ---


def fit(spanlog, prepare, predict, min_picture=0, max_entries=None, tolerance=3, worst=12, channels=3):
    """For every pixel written while an entry is current: predict(ctx, x, y, dest_bytes) vs the written bytes.
    prepare(entry_event) -> ctx or None (skip). Prints and returns (pixels, mean error per channel, pixels off by more
    than tolerance)."""
    ctx = None
    n_ent = n = big = 0
    err = [0.0] * channels
    bad = []
    for ev in events(spanlog, min_picture):
        if isinstance(ev, Entry):
            ctx = prepare(ev)
            if ctx is not None:
                n_ent += 1
                if max_entries and n_ent > max_entries:
                    break
            continue
        if ctx is None or not isinstance(ev, Pixels):
            continue
        for i, (bp, op) in enumerate(zip(ev.dest, ev.out)):
            B, O = chans(bp), chans(op)
            p = predict(ctx, ev.x + i, ev.y, B)
            if p is None:
                continue
            e = [abs(p[c] - O[c]) for c in range(channels)]
            for c in range(channels):
                err[c] += e[c]
            n += 1
            if max(e) > tolerance:
                big += 1
                if len(bad) < worst:
                    bad.append((ev.x + i, ev.y, B, O, [round(v, 1) for v in p]))
    mean = [e / max(1, n) for e in err]
    print('%d entries, %d pixels; mean |error| %s; %d pixels off by > %d' % (
        n_ent, n, ' / '.join('%.2f' % m for m in mean), big, tolerance))
    for b in bad:
        print('  x %d y %d dest %s out %s model %s' % b)
    return n, mean, big

"""Tempest 3000 particles (entry type 00310309, module 806A8990 mode 3, handler 806A8B68): model vs the emulator.

python -I tools/t3k_partfit.py <spanlog> <entrylog> [entry picture offset, default 0]

Logs: python -I tools/t3k_logrun.py --state play_f3150 --fields 20 --span 00310309 --entries --out <prefix>
(span and entry logs number pictures alike: offset 0).

Model: records of 4 words at w12 (16 bytes each, until word 0 = $80000000): w0 x hi / y lo (signed), w1 depth,
w2 colour (YCrCb), w3 intensity hi / size lo. d = max(depth & $7FFFFFFF, $400000); k = 0.75 x 2^44 / (d + $800000)
(reciprocal 806A9566); centre = ((x << 16) + camx x 8) x k >> 19 + 180.5, same for y (camera: 3 words at $800DAD80 ->
local $BE4 (y), $BE8 (x)); size = (lo << 16) x k >> 19; I = (hi << 16) x k >> 19 (2.30), or $3FFF0000 when hi = 0. size <= 30 px. Blob 806A9124: box from floor(centre - size / 2), size + 2 wide, size + 1 high; per pixel
(integer corner p) d2 = |p - centre|^2, k = I (1 - d2 / R^2), R = size / 2, skipped where d2 >= R^2;
out = dest + (colour - dest) x k.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import t3k_spanlib as sl  # noqa: E402


def particles(words, mem):
    cam = sl.read_block(mem, 0x800DAD80, 3)
    camy, camx = sl.s32(cam[0]), sl.s32(cam[1])
    out = []
    a = words[12]
    while a in mem and mem[a] != 0x80000000:
        w0, dep, col, w3 = sl.read_block(mem, a, 4)
        a += 16
        d = max(dep & 0x7FFFFFFF, 0x400000)
        k = 0.75 * 2 ** 44 / (d + 0x800000)
        X = (sl.hi16(w0) << 16) + camx * 8
        Y = sl.s32((w0 << 16) & 0xFFFFFFFF) + camy * 8
        cx = X * k / 2 ** 19 / 65536 + 180.5
        cy = Y * k / 2 ** 19 / 65536 + 120.5
        hi, lo = w3 >> 16, w3 & 0xFFFF
        size = (lo << 16) * k / 2 ** 19 / 65536
        I = (hi << 16) * k / 2 ** 19 / 2 ** 30 if hi else 0x3FFF0000 / 2 ** 30
        size = min(size, 30.0)
        if size <= 0:
            continue
        out.append((cx, cy, size, I, sl.chans(col)))
    return out


def blob(p, x, y, dest):
    cx, cy, size, I, col = p
    R2 = (size / 2) ** 2
    d2 = (x - cx) ** 2 + (y - cy) ** 2
    if d2 >= R2:
        return list(dest[:3])
    k = I * (1 - d2 / R2)
    return [dest[c] + (col[c] - dest[c]) * k for c in range(3)]


def main():
    spans, entlog = sys.argv[1], sys.argv[2]
    ofs = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    ents = sl.load_entries(entlog, 0x00310309)
    # rows arrive blob by blob in list order (each MPE: its band's rows of each blob): attribute each row to the
    # next blob (from the last one used) whose box holds it, starting over at each entry event
    parts, at = None, 0
    n = big = 0
    err = [0.0] * 3
    unmatched = 0
    for ev in sl.events(spans):
        if isinstance(ev, sl.Entry):
            hit = sl.entry_for(ents, sl.Entry(ev.words, ev.picture + ofs, ev.job), match_from=2)
            parts, at = (particles(*hit) if hit else None), 0
            continue
        if not parts or not isinstance(ev, sl.Pixels):
            continue
        found = None
        for j in list(range(at, len(parts))) + list(range(0, at)):
            cx, cy, size = parts[j][:3]
            bx, by = int(cx - size / 2), int(cy - size / 2)
            if bx <= ev.x <= bx + 1 and by <= ev.y <= by + int(size) and len(ev.dest) <= int(size) + 2:
                found = j
                break
        if found is None:
            unmatched += 1
            continue
        at = found
        for i, (bp, op) in enumerate(zip(ev.dest, ev.out)):
            B, O = sl.chans(bp), sl.chans(op)
            pr = blob(parts[found], ev.x + i, ev.y, B)
            e = [abs(pr[c] - O[c]) for c in range(3)]
            for c in range(3):
                err[c] += e[c]
            n += 1
            big += max(e) > 3
    print('%d pixels (%d rows unattributed); mean |error| %.2f / %.2f / %.2f; %d off by > 3' % (
        n, unmatched, err[0] / max(1, n), err[1] / max(1, n), err[2] / max(1, n), big))


if __name__ == '__main__':
    main()

# check_interp.py <a.lines> <b.lines> <t.lines> <t>
# Lines of frame N-1 (a) and N (b) at t = 1, and frame N blended at t: each blended line should be a + (b - a) * t
# (projection is not linear in z, so allow a little). Lines are matched by order (same call list in both frames).
import sys

def load(p):
    return [tuple(map(float, l.split())) for l in open(p)]

a, b, m = load(sys.argv[1]), load(sys.argv[2]), load(sys.argv[3])
t = float(sys.argv[4])
print('lines', len(a), len(b), len(m))
n = min(len(a), len(b), len(m))
worst, moved, tot = 0.0, 0, 0.0
for la, lb, lm in zip(a[:n], b[:n], m[:n]):
    if max(abs(x - y) for x, y in zip(la, lb)) > 0.5:
        moved += 1
    err = max(abs(x + (y - x) * t - z) for x, y, z in zip(la, lb, lm))
    worst = max(worst, err)
    tot += err
print(f'moving lines {moved}, worst error {worst:.3f} px, mean {tot / max(n, 1):.3f} px')

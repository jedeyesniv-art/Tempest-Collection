"""Check original per-packet vector weights from an external Nuance fixture.

The capture must single-step the original vector module, logging its segment,
point, pre-weight and final-weight observation sites. No game data is bundled.
These are packet observations, not a count of unique screen pixels.
"""
import argparse
import csv
import json
from pathlib import Path


def signed(value):
    value &= 0xffffffff
    return value - 0x100000000 if value & 0x80000000 else value


def summarize(path):
    pending = {}
    count = mismatches = over_one = negative = 0
    with path.open() as stream:
        rows = list(csv.reader(stream))
    for row in rows:
        if len(row) != 36:
            raise ValueError('Incomplete stroke observation')
        pc, engine = int(row[0], 16), int(row[1])
        regs = [signed(int(v, 16)) for v in row[2:34]]
        if pc == 0x2030072c:
            pending.pop(engine, None)
        elif pc == 0x20300c52:
            pending[engine] = regs
        elif pc == 0x20300c5e:
            if engine not in pending:
                raise ValueError('Weight has no preceding original packet observation')
            before = pending.pop(engine)
            peak = signed((before[11] * before[31]) >> 29)
            expected = signed((peak * before[2]) >> 16)
            count += 1
            mismatches += expected != regs[2]
            over_one += regs[2] > 0x40000000
            negative += regs[2] < 0
    if not count or pending:
        raise ValueError('Empty or incomplete stroke fixture')
    return dict(weight_observations=count, integer_weight_mismatches=mismatches,
                weights_above_one=over_one, negative_weights=negative)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = summarize(args.capture)
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')
    raise SystemExit(bool(result['integer_weight_mismatches']))

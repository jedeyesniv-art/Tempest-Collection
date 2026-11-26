"""Compare captured player-menu cadence and matched transition feedback runs.
No hardware-speed inference is made from Nuance's fixed dispatch budget.
"""
import argparse
import csv
import json
import statistics
from collections import defaultdict
from pathlib import Path


def menu_rates(root):
    capture = root / 'runs/reference-scene-cadence/player-select-200000'
    metadata = json.loads((capture / 'run.json').read_text())
    assert metadata['run_completed'] and metadata['options'] == 0
    rounds = metadata['fixed_rounds']
    with (capture / 'driver-trace.csv').open() as stream:
        frames = [row for row in csv.DictReader(stream) if row['pc'] == '80015C74']
    intervals = defaultdict(list)
    for before, after in zip(frames, frames[1:]):
        if before['return'] == after['return'] and before['scene'] == after['scene'] == '3':
            duration = (int(after['dispatch_round']) - int(before['dispatch_round'])) / rounds / 60
            if duration > 0:
                intervals[before['return']].append(duration)
    names = {'800188B0': 'entry', '80018D20': 'idle', '80018EC4': 'exit'}
    return {name: {'intervals': len(intervals[pc]),
                   'calls_per_emulated_second': 1 / statistics.mean(intervals[pc]),
                   'seconds_per_64_steps': statistics.mean(intervals[pc]) * 64}
            for pc, name in names.items()}


def feedback(root):
    paths = {policy: root / ('runs/transition-compare-' + policy)
             for policy in ('pinned', 'continuous')}
    result = {name + '_identical': (paths['pinned'] / ('trace.' + name + '.csv')).read_bytes()
              == (paths['continuous'] / ('trace.' + name + '.csv')).read_bytes()
              for name in ('frames', 'values', 'effects')}
    for policy, path in paths.items():
        with (path / 'temporal.csv').open() as stream:
            rows = [r for r in csv.DictReader(stream) if r['scene_kind'] == '7']
        result[policy] = {'samples': len(rows),
                          'mean_rgb': statistics.mean(float(r['mean_rgb']) for r in rows),
                          'mean_absolute_change': statistics.mean(float(r['mean_absolute_change']) for r in rows)}
    result['energy_ratio'] = result['continuous']['mean_rgb'] / result['pinned']['mean_rgb']
    result['change_ratio'] = result['continuous']['mean_absolute_change'] / result['pinned']['mean_absolute_change']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile-root', type=Path, default=Path(__file__).resolve().parents[2] / 'tempest-profile')
    args = parser.parse_args()
    result = {'menu': menu_rates(args.profile_root), 'feedback': feedback(args.profile_root),
              'limits': 'Menu rates describe the configured emulator, not calibrated hardware. RGB change is a temporal difference metric, not a perceptual flicker score. CRT/interpolation off.'}
    (args.profile_root / 'menu-transition-comparison.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if all(result['feedback'][name + '_identical'] for name in ('frames', 'values', 'effects')) else 1


if __name__ == '__main__':
    raise SystemExit(main())

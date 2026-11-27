"""Replay original drawing captures through the production renderer.

No Nuance execution is linked into the replay executable. All fixtures remain
external, user-owned files. This compares original raster pixels, rather than
using the CPU version of the replacement renderer as an oracle for its GPU.
"""
import argparse
import json
from pathlib import Path
import subprocess


def compare(binary, folder, output):
    output.mkdir(parents=True, exist_ok=True)
    with (folder / 'events.jsonl').open() as stream:
        events = [json.loads(line) for line in stream]
    presents = {e['picture']: e for e in events if e['kind'] == 'present'}
    snapshots = {e['picture']: e for e in events
                 if e['kind'] == 'snapshot' and e['reason'] == 'present'}
    last = max(presents, default=-1)
    results = []
    for system in sorted(folder.glob('renderer-*-system.bin')):
        index = int(system.name.split('-')[1])
        prefix = f'renderer-{index:03d}'
        if index not in presents:
            continue
        reference = None
        if index in snapshots:
            snapshot = snapshots[index]
            reference = folder / next(b['file'] for b in snapshot['buffers']
                                      if b['base'] == snapshot['base'])
        elif index == last and (folder / 'final-main.bin').exists():
            data = (folder / 'final-main.bin').read_bytes()
            offset = presents[index]['base'] - 0x40000000
            if not 0 <= offset <= len(data) - 360 * 240 * 4:
                raise ValueError('Presented framebuffer is outside captured RAM')
            reference = output / f'original-{index:03d}.raw'
            reference.write_bytes(data[offset:offset + 360 * 240 * 4])
        if reference is None:
            continue
        # The last completed state avoids treating live DMA source timing as
        # an implementation gap. Earlier frames require their own snapshot.
        completed = index == last and (folder / 'final-system.bin').exists()
        main = folder / ('final-main.bin' if completed else prefix + '-main.bin')
        system = folder / 'final-system.bin' if completed else system
        image = output / f'native-{index:03d}.bmp'
        args = [binary, system, main, folder / (prefix + '-local.bin'), image, reference]
        process = subprocess.run(list(map(str, args)), capture_output=True, text=True)
        result = dict(picture=index, capture=str(folder.resolve()),
                      data_boundary='completed' if completed else 'renderer start',
                      native_image=str(image.resolve()), original_raw=str(reference.resolve()),
                      exit=process.returncode, output=process.stdout, error=process.stderr)
        results.append(result)
        if process.returncode:
            raise RuntimeError(result)
        print(folder.name, index, process.stdout.strip().splitlines()[-1], flush=True)
    if not results:
        raise RuntimeError(f'No complete renderer/reference pair in {folder}')
    (output / 'comparison.json').write_text(json.dumps(results, indent=2) + '\n')
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    compare(args.binary.resolve(), args.capture.resolve(), args.output.resolve())

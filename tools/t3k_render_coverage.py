"""Inventory original module boundaries, dispatch tables and captured draw modes.

Inputs are user-owned external Nuance captures; no game data is bundled.
This inventories evidence, not a claim that recognized commands are complete.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import struct
import hashlib
from collections import Counter

COMPUTE = {2, 3, 6, 7, 10, 12, 17}
NAMES = {0: 'rectangle/copy/plane', 1: 'vector strokes', 2: 'shape preparation',
         3: 'texture preparation', 4: 'zoom feedback', 5: 'textured rings',
         6: 'data animation', 7: 'numeric preparation', 8: 'point rings/jump',
         9: 'particle/point drawing', 10: 'web preparation', 11: 'textured surfaces',
         12: 'projection preparation', 13: 'glowing numbers', 14: 'disc shapes',
         15: 'ripple feedback', 16: 'disc grid', 17: 'depth/course preparation',
         18: 'drawing coordinator'}
# Reviewed boundaries: adjacent pointer runs can combine command dispatch and
# pixel-row helpers. Do not count the whole run as primary command modes.
PRIMARY = {0: (0x20100E44, 6), 8: (0x20100B80, 3), 9: (0x20100BC0, 6),
           13: (0x20100E78, 1), 14: (0x20100BD0, 3), 16: (0x20100BE0, 1)}


def inventory(image, output, dll=None, captures=(), entry_traces=(), native_source=None):
    data = image.read_bytes()
    if len(data) != 0x800000:
        raise ValueError('Expected complete original 8 MiB system snapshot')
    output.mkdir(parents=True, exist_ok=True)
    decode = None
    if dll:
        core = ctypes.CDLL(str(dll.resolve()))
        core.nuance_research_init_tables()
        decode = core.nuance_research_disasm_module
        decode.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint,
                           ctypes.c_uint, ctypes.c_char_p]
        decode.restype = ctypes.c_bool
    modules = []
    recognized = set()
    if native_source:
        code_text = native_source.read_text(encoding='utf-8')
        section = code_text[code_text.index('void BuildFrom('):code_text.index('bool BuildPictureLive(')]
        recognized = {int(v, 16) for v in re.findall(r'case\s+0x([0-9A-Fa-f]+)\s*:', section)}
    for module in range(19):
        words = struct.unpack_from('>8I', data, 0xB14A0 + module * 32)
        code, load, size, _, source, destination, length, _ = words
        if not (0x80000000 <= code < 0x80800000 and
                0x20300000 <= load < load + size <= 0x20302000 and
                0x80000000 <= source <= source + length < 0x80800000):
            raise ValueError(f'Invalid original module {module}')
        # Consecutive in-module pointers are candidate dispatch/row tables.
        # Retain their exact addresses; semantic classification needs review.
        tables, run = [], []
        for offset in range(0, length // 4 * 4, 4):
            value = struct.unpack_from('>I', data, (source & 0x7fffff) + offset)[0]
            if load <= value < load + size:
                run.append((destination + offset, value))
            else:
                if run:
                    tables.append(dict(address=f'{run[0][0]:08X}',
                                       targets=[f'{v:08X}' for _, v in run]))
                    run = []
        if run:
            tables.append(dict(address=f'{run[0][0]:08X}',
                               targets=[f'{v:08X}' for _, v in run]))
        assembly = output / f'module-{module:02d}.asm.txt'
        if decode and not decode(os.fsencode(image.resolve()), code, size, load,
                                 os.fsencode(assembly.resolve())):
            raise RuntimeError(f'Cannot disassemble module {module}')
        primary = []
        if module in PRIMARY:
            base, count = PRIMARY[module]
            for mode in range(count):
                offset = base + mode * 4 - destination
                if not (0 <= offset <= length - 4):
                    raise ValueError(f'Module {module} primary table is outside its data segment')
                target = struct.unpack_from('>I', data, (source & 0x7fffff) + offset)[0]
                if not load <= target < load + size:
                    raise ValueError(f'Module {module} primary target is outside its code segment')
                mapped = sorted(f'{v:08X}' for v in recognized if v & 255 == module and (v >> 8) & 127 == mode)
                primary.append(dict(mode=mode, target=f'{target:08X}', native_types=mapped,
                                    status='mapped; semantics require review' if mapped else 'not mapped by shared decoder; usage unproven'))
        modules.append(dict(module=module, name=NAMES[module],
                            role='compute' if module in COMPUTE else 'coordinator' if module == 18 else 'raster',
                            code=f'{code:08X}', load=f'{load:08X}', code_bytes=size,
                            data=f'{source:08X}', data_load=f'{destination:08X}',
                            data_bytes=length, candidate_tables=tables,
                            code_sha256=hashlib.sha256(data[(code & 0x7fffff):(code & 0x7fffff)+size]).hexdigest(),
                            primary_modes=primary,
                            coverage='requires semantic mode/flag review'))
    observed = Counter()
    for folder in captures:
        for pattern in ('snapshot-*-mpe0-data.raw', 'job-*-mpe0-data.raw'):
            for path in folder.glob(pattern):
                raw = path.read_bytes()
                if len(raw) < 0x70:
                    continue
                entry = struct.unpack_from('>16I', raw, 0x30)
                kind = entry[15]
                if kind & 255 >= 19:
                    continue
                observed[(str(folder), f'{kind:08X}', f'{entry[1]:08X}',
                          f'{entry[7] & 127:02X}', f'{entry[13] & 127:02X}')] += 1
    traces = []
    import csv
    for path in entry_traces:
        with path.open() as stream:
            rows = list(csv.DictReader(stream))
        if any(not row.get('w15') or not re.fullmatch('[0-9A-F]{8}', row['w15']) for row in rows):
            raise ValueError(f'Incomplete entry trace: {path}')
        types = Counter(row['w15'] for row in rows)
        traces.append(dict(path=str(path), samples=len(rows), scenes=dict(Counter(row['scene'] for row in rows)),
                           types=dict(types), unsupported_types=[v for v in sorted(types) if recognized and int(v, 16) not in recognized],
                           row_signatures=[dict(type=t, word7_low=s, samples=n) for (t, s), n in sorted(Counter((row['w15'], int(row['w7'], 16) & 127) for row in rows if int(row['w15'], 16) & 255 in (4, 8, 9, 14, 16)).items())]))
    report = dict(modules=modules, original_entry_traces=traces, captured_samples=[dict(capture=c, type=t,
                  word1=w, word7_selector=s7, word13_selector=s13, samples=n)
                  for (c, t, w, s7, s13), n in sorted(observed.items())],
                  limitations=['Data-pointer runs are candidate tables, not automatically proven dispatch modes.',
                               'Snapshot samples do not prove every command invocation or scene is covered.',
                               'Recognition and CPU/GPU agreement do not prove equivalence to original rasterization.'])
    (output / 'coverage-inventory.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--dll', type=Path)
    parser.add_argument('--capture', action='append', type=Path, default=[])
    parser.add_argument('--entries', action='append', type=Path, default=[])
    parser.add_argument('--native-source', type=Path)
    args = parser.parse_args()
    report = inventory(args.image, args.output, args.dll, args.capture, args.entries, args.native_source)
    print(f"{len(report['modules'])} original modules; {len(report['captured_samples'])} sampled mode signatures")

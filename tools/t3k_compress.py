"""Compress the Tempest 3000 disc image: .iso (743 MB) -> .nuz (~196 MB), read by our Nuance core (disc_image.h).

The disc is /NUON/nuon.run (program + all game data), nuon.n16 and nuon.dat - and nuon.dat is nothing but the 19
music tracks, 16-bit big-endian stereo PCM at 32 kHz, located by a (start, end) block table in nuon.run at +0x104340.
Each track becomes one Ogg Vorbis stream (lossy, default level 0.2 = ~260 kbps on average); every other sector is stored as is.
The game streams the music with MediaRead and the core decodes the sectors it asks for.

After writing and checking the .nuz the tool ASKS before deleting the original .iso (default: keep it).

python -I tools/t3k_compress.py "<Tempest 3000.iso>" [--out <file.nuz>] [--level 0.2] [--no-delete-prompt]
"""
import argparse
import hashlib
import io
import os
import struct
import sys
from concurrent.futures import ProcessPoolExecutor

SECTOR = 2048
TABLE_OFF = 0x104340   # in nuon.run: 19 x (start block, end block) big-endian
TRACKS = 19
RATE = 32000           # measured: tools/t3k_musicrate.py (correlation 0.996 at 32 kHz)
MAGIC = b'LLNUONZ1'


def iso_files(f):
    """{upper-case path: (lba, size)} for every file of the ISO9660 image."""
    f.seek(16 * SECTOR)
    pvd = f.read(SECTOR)
    if pvd[1:6] != b'CD001':
        sys.exit('not an ISO9660 image')
    total = struct.unpack_from('<I', pvd, 80)[0]
    out = {}

    def walk(lba, size, path):
        f.seek(lba * SECTOR)
        data = f.read(size)
        i = 0
        while i < len(data):
            n = data[i]
            if n == 0:
                i = (i // SECTOR + 1) * SECTOR
                continue
            ext, ln = struct.unpack_from('<I', data, i + 2)[0], struct.unpack_from('<I', data, i + 10)[0]
            flags, nl = data[i + 25], data[i + 32]
            name = data[i + 33:i + 33 + nl]
            i += n
            if name in (b'\x00', b'\x01'):
                continue
            full = path + '/' + name.decode('ascii', 'replace').split(';')[0].upper()
            if flags & 2:
                walk(ext, ln, full)
            else:
                out[full] = (ext, ln)

    root = pvd[156:190]
    walk(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0], '')
    return total, out


def encode_track(args):
    iso, lba, start, end, level = args
    import numpy as np
    import soundfile as sf
    with open(iso, 'rb') as f:
        f.seek((lba + start) * SECTOR)
        pcm = np.frombuffer(f.read((end - start) * SECTOR), dtype='>i2').reshape(-1, 2).astype(np.int16)
    b = io.BytesIO()
    # libsndfile overflows the stack on one huge write: feed it in chunks
    with sf.SoundFile(b, 'w', RATE, 2, format='OGG', subtype='VORBIS', compression_level=level) as o:
        for i in range(0, len(pcm), 4096):
            o.write(pcm[i:i + 4096])
    ogg = b.getvalue()
    # check: decodes to exactly the track's frames; signal-to-noise for the report
    b.seek(0)
    frames = 0
    sig = err = 0.0
    with sf.SoundFile(b) as r:
        for blk in r.blocks(65536, dtype='int16'):
            ref = pcm[frames:frames + len(blk)].astype(np.float64)
            sig += float((ref * ref).sum())
            err += float(((blk.astype(np.float64) - ref) ** 2).sum())
            frames += len(blk)
    if frames != len(pcm):
        raise RuntimeError(f'track {start}..{end}: decoded {frames} frames, expected {len(pcm)}')
    snr = 10 * np.log10(sig / err) if err > 0 else float('inf')
    return start, end, ogg, snr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('iso')
    ap.add_argument('--out')
    ap.add_argument('--level', type=float, default=0.2, help='libsndfile Vorbis compression level (0 best .. 1)')
    ap.add_argument('--no-delete-prompt', action='store_true', help='never offer to delete the .iso')
    a = ap.parse_args()
    out = a.out or os.path.splitext(a.iso)[0] + '.nuz'

    with open(a.iso, 'rb') as f:
        total, files = iso_files(f)
        if '/NUON/NUON.DAT' not in files or '/NUON/NUON.RUN' not in files:
            sys.exit('no /NUON/nuon.dat + nuon.run: not the Tempest 3000 disc')
        dat_lba, dat_size = files['/NUON/NUON.DAT']
        run_lba, run_size = files['/NUON/NUON.RUN']
        f.seek(run_lba * SECTOR + TABLE_OFF)
        w = struct.unpack('>%dI' % (2 * TRACKS), f.read(8 * TRACKS))
    tracks = sorted((w[2 * i], w[2 * i + 1]) for i in range(TRACKS))
    dat_sectors = dat_size // SECTOR
    if (dat_size % SECTOR or tracks[0][0] != 0 or tracks[-1][1] != dat_sectors
            or any(tracks[i][1] != tracks[i + 1][0] for i in range(TRACKS - 1))):
        sys.exit('the music table in nuon.run does not cover nuon.dat: a different version of the disc?')
    print(f'{a.iso}: {total} sectors, nuon.dat {dat_size / 1e6:.1f} MB = {TRACKS} tracks at sector {dat_lba}')

    with ProcessPoolExecutor() as ex:
        enc = list(ex.map(encode_track, [(a.iso, dat_lba, s, e, a.level) for s, e in tracks]))
    for s, e, ogg, snr in enc:
        secs = (e - s) * SECTOR / 4 / RATE
        print(f'  track {s:6d}..{e:6d}  {secs / 60:5.2f} min  {len(ogg) / 1e6:6.2f} MB  '
              f'{len(ogg) * 8 / secs / 1000:4.0f} kbps  SNR {snr:5.1f} dB')

    # segments: raw before nuon.dat, a vorbis stream per track, raw after
    segs = [(0, dat_lba, 0, None)]
    segs += [(dat_lba + s, e - s, 1, ogg) for s, e, ogg, _ in enc]
    segs.append((dat_lba + dat_sectors, total - dat_lba - dat_sectors, 0, None))
    head = 32 + 32 * len(segs)
    tmp = out + '.tmp'
    raw_md5 = hashlib.md5()
    with open(a.iso, 'rb') as f, open(tmp, 'wb') as o:
        o.write(MAGIC + struct.pack('<IIIIII', 1, total, len(segs), 0, 0, 0))
        offset = head
        table = []
        for first, count, kind, ogg in segs:
            size = count * SECTOR if kind == 0 else len(ogg)
            table.append(struct.pack('<IIIIQQ', first, count, kind, 0, offset, size))
            offset += size
        o.write(b''.join(table))
        for first, count, kind, ogg in segs:
            if kind == 0:
                f.seek(first * SECTOR)
                d = f.read(count * SECTOR)
                if len(d) != count * SECTOR:
                    sys.exit('short read from the ISO')
                raw_md5.update(d)
                o.write(d)
            else:
                o.write(ogg)
    # check the raw sectors made it unchanged
    check = hashlib.md5()
    with open(tmp, 'rb') as o:
        o.seek(head)
        for first, count, kind, ogg in segs:
            if kind == 0:
                check.update(o.read(count * SECTOR))
            else:
                o.seek(len(ogg), 1)
    if check.digest() != raw_md5.digest():
        sys.exit('raw sector check failed: ' + tmp + ' kept for a look')
    os.replace(tmp, out)
    iso_size, nuz_size = os.path.getsize(a.iso), os.path.getsize(out)
    print(f'wrote {out}: {nuz_size / 1e6:.1f} MB ({100 * nuz_size / iso_size:.0f}% of {iso_size / 1e6:.1f} MB)')

    if a.no_delete_prompt:
        return
    links = os.stat(a.iso).st_nlink
    note = (f' It has {links - 1} other hard link(s): deleting this name frees no space until those go too.'
            if links > 1 else '')
    ans = input(f'Delete the original {a.iso}?{note} Type yes to delete, anything else keeps it: ')
    if ans.strip().lower() == 'yes':
        os.remove(a.iso)
        print('deleted', a.iso)
    else:
        print('kept', a.iso)


if __name__ == '__main__':
    main()

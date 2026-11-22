"""Vendors the two heavily patched emulator cores into the main repository (as Llamatron does with Virtual Jaguar):
third_party/virtualjaguar and third_party/NuanceResurrection become plain folders whose build-relevant files the
outer git tracks. Their own git histories are kept beside them (third_party/_coregit/, not in git) so nothing is lost.

python -I tools/vendor_cores.py stage <dir>     copy the trimmed trees into <dir>/<core> (trial builds; no changes)
python -I tools/vendor_cores.py apply           do it in place: history -> _coregit, untracked-by-us files removed

The trimmed set: Virtual Jaguar = the files Llamatron vendors (C:/LlamatronRecomp, same branch); Nuance = what its CMake
build reads (CMakeLists, src, external incl. the asmjit submodule's files, third_party, compat, the BIOS replacement
.cof files, shaders, licence, readme). Left out: upstream's tests, website, docs, CI, and Nuance's debugging dumps.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
TP = os.path.join(ROOT, 'third_party')
COREGIT = os.path.join(TP, '_coregit')
LLAMA_VJ = r'C:\LlamatronRecomp'


def git(cwd, *args):
    return subprocess.run(['git', '-C', cwd] + list(args), check=True, capture_output=True, text=True,
                          encoding='utf-8').stdout


def vj_files():
    files = git(LLAMA_VJ, 'ls-files', 'third_party/virtualjaguar').splitlines()
    files = [f[len('third_party/virtualjaguar/'):] for f in files]
    # upstream's AI-tool configs are no use here
    return [f for f in files if f.split('/')[0] not in ('.agents', '.claude', '.codex') and f != 'LLAMASOFT-NOTES.txt']


NUANCE_ROOT_FILES = ['CMakeLists.txt', 'CMakePresets.json', 'License.txt', 'README.md', 'bios.cof', 'minibios.cof',
                     'minibiosX.cof', 'video_generic.vs', 'video_m32_o32.fs', 'video_m32_o32_crt.fs',
                     'dist/nuance_libretro.info']


def nuance_files():
    d = os.path.join(TP, 'NuanceResurrection')
    out = []
    for f in git(d, 'ls-files').splitlines():
        top = f.split('/')[0]
        if f in NUANCE_ROOT_FILES or top in ('src', 'third_party', 'compat') or (top == 'external' and f != 'external/asmjit'):
            if os.path.isfile(os.path.join(d, f)):
                out.append(f)
    for f in git(os.path.join(d, 'external', 'asmjit'), 'ls-files').splitlines():
        out.append('external/asmjit/' + f)
    return out


CORES = {
    'virtualjaguar': (vj_files, '''Virtual Jaguar (libretro/virtualjaguar-libretro, GPL-3, see LICENSE), vendored for Tempest 2000 (t2k.exe).
Source: our branch llamasoft-jaguar, commit eb83a9de (from libretro master 34696082, v3.11.0) - 9 patches:
host port $DFFE00-$DFFEFF, line-A host traps, OP skip ranges, survey hooks (GPU go / blit / OP object read),
GPU load / store hooks, a GPU/DSP late write-back fix, vjEepromFromSaveBuf. Changes since are in this repository's
history. The same core is vendored in Llamasoft on the ST (Defender 2000): a core fix may belong in both.
Trimmed to what the build needs: the website, docs, tests, CI files and the core's own app (app/ keeps cmake/
only) were left out.
'''),
    'NuanceResurrection': (nuance_files, '''Nuance Resurrection (andkrau/NuanceResurrection, see License.txt: the original author's terms for his code, BSD-3-Clause
for the open-source contributions), vendored for Tempest 3000 (t3k.exe, nuance_libretro.dll).
Source: our branch llamasoft-nuon (from upstream 12ece855): the libretro host additions, the capture of each picture's
display list for our native renderer, the draw gate, run-ahead snapshots, the compressed disc image (.nuz, disc_image.cpp
+ stb_vorbis), .mdf discs, high-resolution patches, save states. Changes since are in this repository's history.
external/asmjit: the asmjit submodule (zlib licence) at 0bd5787b, its files included.
Trimmed to what the build reads: upstream's debugging dumps, Visual Studio projects, docs and CI were left out.
bios.cof / minibios*.cof are Nuance's own BIOS replacement (no VM Labs code).
'''),
}


def stage(dst):
    for name, (lister, notes) in CORES.items():
        src = os.path.join(TP, name)
        out = os.path.join(dst, name)
        if os.path.exists(out):
            shutil.rmtree(out)
        files = lister()
        for f in files:
            to = os.path.join(out, f)
            os.makedirs(os.path.dirname(to), exist_ok=True)
            shutil.copy2(os.path.join(src, f), to)
        open(os.path.join(out, 'LLAMASOFT-NOTES.txt'), 'w', encoding='utf-8', newline='\n').write(notes)
        print('%s: %d files -> %s' % (name, len(files), out))


def apply():
    os.makedirs(COREGIT, exist_ok=True)
    for name, (lister, notes) in CORES.items():
        src = os.path.join(TP, name)
        if git(src, 'status', '--porcelain', '--untracked-files=no').strip():
            sys.exit('%s has uncommitted changes: commit them on its branch first' % name)
    for name, (lister, notes) in CORES.items():
        src = os.path.join(TP, name)
        keep = set(f.replace('/', os.sep) for f in lister())
        bundle = os.path.join(COREGIT, name + '.bundle')
        git(src, 'bundle', 'create', bundle, '--all')
        if name == 'NuanceResurrection':   # the submodule's .git file points into the parent's .git\modules
            gf = os.path.join(src, 'external', 'asmjit', '.git')
            if os.path.isfile(gf):
                shutil.move(gf, os.path.join(COREGIT, 'NuanceResurrection.asmjit.gitfile'))
        shutil.move(os.path.join(src, '.git'), os.path.join(COREGIT, name + '.git'))
        removed = 0
        for d, dirs, files in os.walk(src, topdown=False):
            for f in files:
                rel = os.path.relpath(os.path.join(d, f), src)
                if rel not in keep and rel != os.path.join('src', 'core', 'version.h'):   # (generated by the build)
                    os.remove(os.path.join(d, f))
                    removed += 1
            if not os.listdir(d):
                os.rmdir(d)
        open(os.path.join(src, 'LLAMASOFT-NOTES.txt'), 'w', encoding='utf-8', newline='\n').write(notes)
        print('%s: kept %d files, removed %d; history in %s (+ .bundle)' % (name, len(keep), removed,
                                                                           os.path.join(COREGIT, name + '.git')))


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == 'stage':
        stage(sys.argv[2])
    elif len(sys.argv) == 2 and sys.argv[1] == 'apply':
        apply()
    else:
        sys.exit(__doc__)

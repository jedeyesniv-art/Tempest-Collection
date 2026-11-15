#!/bin/sh
# Builds the MAME libretro core (third_party/mame-libretro, libretro/mame) with only the Tempest driver
# (src/mame/atari/tempest.cpp) -> third_party/mame-libretro/tempest_libretro.dll.
# Run from Git Bash: MAME's makefiles want a POSIX shell + coreutils (Git's), and the compiler is the MinGW-w64 gcc
# that ships with the portable Strawberry Perl in third_party/strawberry (GNU make 4.4 too). Extra args go to make
# (e.g. -j8, REGENIE=1).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MINGW="$ROOT/third_party/strawberry/c"
export PATH="$MINGW/bin:$PATH"
export MSYSTEM=MINGW64
cd "$ROOT/third_party/mame-libretro"
# The libretro OSD's Windows key table wants SDL headers (its CI has MSYS2's SDL2): our SDL3 headers do, through
# SDLMAME_SDL3 (only the scancode names are used; nothing links SDL).
mingw32-make -f Makefile.libretro platform=win MINGW64="$MINGW" CC=gcc CXX=g++ AR=ar \
  ARCHOPTS="-DSDLMAME_SDL3 -I$ROOT/third_party/SDL/include" \
  SUBTARGET=tempest SOURCES=src/mame/atari/tempest.cpp PYTHON_EXECUTABLE=python NOWERROR=1 \
  -j"$(nproc)" "$@"

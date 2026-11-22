#!/usr/bin/env bash
# Fetches the third-party sources this repository builds with but does not carry, at the exact commits we use, and
# applies our patches (third_party/patches/). Run once after cloning, from Git Bash:  bash tools/get_third_party.sh
#
# In the repository already (vendored, patched): third_party/virtualjaguar (Tempest 2000), third_party/NuanceResurrection
# (Tempest 3000), third_party/libsndfile (an unmodified DLL). Fetched here:
#   third_party/SDL             SDL 3.4.16 (zlib), unmodified
#   third_party/mame-libretro   libretro/mame at 5f5a0774 (MAME 0.289) + our patch (the vector line export for t81),
#                               branch llamasoft-mame - built with tools/build_mame.sh (only the Tempest driver)
#   third_party/vita3k-libretro WizzardSK/vita3k-libretro at 29ad0e79 + our 4 patches, branch llamasoft-vita, with its
#                               own submodules - built with tools/build_vita3k.bat
# A folder that already exists is left alone. Not fetched (a tool, not source): Strawberry Perl Portable 5.42.3.1
# 64-bit (strawberryperl.com), unzipped to third_party/strawberry - build_mame.sh uses its MinGW gcc + GNU make,
# build_vita3k.bat its perl.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/third_party"
PATCHES="$TP/patches"

# The commit at <url>, as branch <branch>, with <patch dir>'s patches applied on top (git am: the same commits).
fetch_at() {
  local dir="$1" url="$2" sha="$3" branch="$4" patches="$5"
  if [ -e "$dir" ]; then echo "exists, left alone: $dir"; return; fi
  echo "== $dir <- $url @ ${sha:0:8}"
  git init -q "$dir"
  git -C "$dir" remote add origin "$url"
  git -C "$dir" fetch -q --depth 1 origin "$sha"
  git -C "$dir" checkout -q -b "$branch" FETCH_HEAD
  if [ -n "$patches" ]; then
    git -C "$dir" -c user.name=build -c user.email=build@localhost am -q --committer-date-is-author-date "$patches"/*.patch
    echo "   applied $(ls "$patches"/*.patch | wc -l) patch(es)"
  fi
}

if [ -e "$TP/SDL" ]; then echo "exists, left alone: $TP/SDL"
else
  echo "== SDL 3.4.16"
  git clone -q --depth 1 --branch release-3.4.16 https://github.com/libsdl-org/SDL "$TP/SDL"
fi

fetch_at "$TP/mame-libretro" https://github.com/libretro/mame 5f5a07748ce5c239c4118463ce8e6473743d4859 \
  llamasoft-mame "$PATCHES/mame-libretro"

if [ ! -e "$TP/vita3k-libretro" ]; then
  fetch_at "$TP/vita3k-libretro" https://github.com/WizzardSK/vita3k-libretro 29ad0e79ec933acb3e1350bdeff5f670a3feb4d8 \
    llamasoft-vita "$PATCHES/vita3k-libretro"
  echo "   submodules (large)..."
  git -C "$TP/vita3k-libretro" submodule update -q --init --recursive
else
  echo "exists, left alone: $TP/vita3k-libretro"
fi

[ -e "$TP/strawberry/perl/bin/perl.exe" ] || echo "NOTE: unzip Strawberry Perl Portable 5.42.3.1 64-bit to third_party/strawberry (for build_mame.sh / build_vita3k.bat)"
echo "done"

# Builds and assembles the Tempest Collection play folder: dist\TempestCollection\ (the owner's own copy, for their
# own devices - dist\ is not in git and holds the games: never publish it).
#
#   TempestCollection.exe          the launcher (build\tempest.exe)
#   libsndfile_x64.dll + "libsndfile COPYING.txt"   the launcher's Vorbis encoder (T3K disc compression; LGPL)
#   t2k.exe t3k.exe txk.exe t81.exe   the games' programs (the launcher starts them by these names)
#   vita3k_libretro.dll tempest_libretro.dll   the emulator cores
#   msvcp140*.dll vcruntime140*.dll    the C++ runtime the Vita3K core needs (app-local; the rest are static)
#   system\vita3k\                 the PS Vita firmware, already installed (vs0 / sa0 / os0 ...; no .pup files)
#   game\                          the games: Tempest 2000 .jag, Tempest 3000 .nuz (or .iso), TxK folder, tempest.zip (MAME)
#   userdata\                      the owner's settings, saves and save states (fullscreen on for every program)
#   README.txt                     Steam Deck steps and controls
#
# Game files are hard-linked from the repo (no second copy on this disk; copying the folder elsewhere copies them).
# Usage: powershell -ExecutionPolicy Bypass -File tools\package_collection.ps1 [-SkipBuild] [-Release]
#
# -Release: the PUBLIC package instead - dist\release\TempestCollection\ + dist\TempestCollection-<date>.zip:
#   no game files (game\ holds a note), no PS Vita firmware (vita_firmware\ holds a note; nothing in system\vita3k),
#   an empty userdata\ (each program writes its defaults on first start), licences\ with the cores' licence texts and
#   our changes to each core as patches. Tempest 3000 runs natively and needs no Nuon BIOS.
param([switch]$SkipBuild, [switch]$Release)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo "build"
$dist = if ($Release) { Join-Path $repo "dist\release\TempestCollection" } else { Join-Path $repo "dist\TempestCollection" }

if (-not $SkipBuild) {
    $log = Join-Path $build "package_build.log"
    cmd /c "`"$repo\build.bat`" > `"$log`" 2>&1"
    if ($LASTEXITCODE -ne 0 -or (Select-String -Path $log -Pattern "FAILED|error:" -CaseSensitive -Quiet)) {
        Write-Error "build failed: see $log"
    }
    cmd /c "`"$repo\tools\build_t4k_helper.bat`" > `"$build\t4k_helper_build.log`" 2>&1"
    if ($LASTEXITCODE -ne 0) { Write-Error "Tempest 4000 helper build failed: see $build\t4k_helper_build.log" }
    cmd /c "`"$repo\tools\build_sg_helper.bat`" > `"$build\sg_helper_build.log`" 2>&1"
    if ($LASTEXITCODE -ne 0) { Write-Error "Space Giraffe helper build failed: see $build\sg_helper_build.log" }
}

# The play folder's own userdata (settings and saves made while playing it) is kept: set aside here, put back below.
# If a run stopped between the two, the set-aside copy is still in build\ and is used (the play folder has none then).
$kept = Join-Path $build "userdata_play_kept"
$distUd = Join-Path $dist "userdata"
if ((-not $Release) -and (Test-Path $distUd)) {
    if (Test-Path $kept) { Remove-Item -Recurse -Force $kept }
    Move-Item $distUd $kept
}
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
foreach ($d in @("", "game", "system", "userdata")) { New-Item -ItemType Directory -Force (Join-Path $dist $d) | Out-Null }

# Programs and cores.
Copy-Item (Join-Path $build "tempest.exe") (Join-Path $dist "TempestCollection.exe")
# libsndfile (LGPL, loaded at run time) for the launcher's Tempest 3000 disc compression (.iso -> .nuz), with its licence.
Copy-Item (Join-Path $build "libsndfile_x64.dll") $dist
Copy-Item (Join-Path $build "COPYING") (Join-Path $dist "libsndfile COPYING.txt")
# The launcher's art (tools/launcher_art.py -> art\panels\: each game's cover and logo; alternative covers stay out).
$panels = Join-Path $repo "art\panels"
if (Test-Path $panels) {
    New-Item -ItemType Directory -Force (Join-Path $dist "art") | Out-Null
    foreach ($id in @("t81", "t2k", "t3k", "sg", "txk", "t4k", "more")) {
        foreach ($f in @("$id.png", "${id}_logo.png")) {
            if (Test-Path (Join-Path $panels $f)) { Copy-Item (Join-Path $panels $f) (Join-Path $dist "art") }
        }
    }
    # MORE LLAMASOFT's list pictures (tools/more_art.py)
    Get-ChildItem $panels -Filter "more_*.png" | Copy-Item -Destination (Join-Path $dist "art")
} else {
    Write-Warning "no art\panels (run tools\launcher_art.py): the launcher will show placeholders"
}
# The launcher's sound stings (tools/launcher_sounds.py -> art\sounds\: each game's lane click and death).
$sounds = Join-Path $repo "art\sounds"
if (Test-Path $sounds) {
    New-Item -ItemType Directory -Force (Join-Path $dist "sounds") | Out-Null
    Get-ChildItem $sounds -Filter "*.wav" | Where-Object { $_.Name -match "_(move|launch)\.wav$" } |
        ForEach-Object { Copy-Item $_.FullName (Join-Path $dist "sounds") }
} else {
    Write-Warning "no art\sounds (run tools\launcher_sounds.py): the launcher will be silent"
}
# Steam library art for the shortcut (tools/steam_art.py -> art\steam\; README step 5).
$steamart = Join-Path $repo "art\steam"
if (Test-Path $steamart) {
    Copy-Item -Recurse $steamart (Join-Path $dist "steam_art")
} else {
    Write-Warning "no art\steam (run tools\steam_art.py): no Steam library art in the package"
}
foreach ($f in @("t2k.exe", "t3k.exe", "txk.exe", "t81.exe", "vita3k_libretro.dll",
                 "tempest_libretro.dll")) {
    Copy-Item (Join-Path $build $f) $dist
}
$crt = Get-ChildItem "$env:ProgramFiles\Microsoft Visual Studio\18\Community\VC\Redist\MSVC" -Directory |
       Sort-Object Name -Descending | ForEach-Object { Get-ChildItem $_.FullName -Recurse -Directory -Filter "Microsoft.VC*.CRT" } |
       Where-Object { $_.FullName -match "\\x64\\" -and $_.FullName -notmatch "onecore|debug" } | Select-Object -First 1
if (-not $crt) { Write-Error "no x64 C++ runtime in the Visual Studio redist folder" }
foreach ($f in @("msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll")) {
    Copy-Item (Join-Path $crt.FullName $f) $dist
}

# System files: installed Vita firmware, unpacked again by its core if missing.
if (-not $Release) {
$vsrc = Join-Path $build "system\vita3k"
$vdst = Join-Path $dist "system\vita3k"
if (-not (Test-Path (Join-Path $vsrc "vs0"))) { Write-Error "Vita firmware not installed yet: run build\txk.exe once" }
New-Item -ItemType Directory -Force $vdst | Out-Null
# Not storage_location.txt: it names the folder the core last used, and a core that finds another folder there MOVES
# the data from it (a test run from dist\ once moved build\system\vita3k away). Without it the copy is a fresh start.
Get-ChildItem $vsrc | Where-Object { $_.Extension -ne ".pup" -and $_.Name -notin @("log", "cache", "storage_location.txt") } |
    ForEach-Object { Copy-Item -Recurse $_.FullName (Join-Path $vdst $_.Name) }
}

if ($Release) {
@"
PUT YOUR GAMES HERE
===================

The Tempest Collection comes without any games. Put your own copies in this folder:

  Tempest (1981 arcade)   tempest.zip             the MAME romset (MAME 0.289 or later; a merged set also gives
                                                  Tempest Tubes in the game's menu)
  Tempest 2000 (Jaguar)   <name>.jag or .j64      the cartridge image; the file name must contain "Tempest"
  Tempest 3000 (Nuon)     <name>.iso or .mdf      the disc image; the file name must contain "Tempest"
                                                  (the launcher offers to compress it on its first start)
  TxK (PS Vita)           PCSB00494\              the game's folder (eboot.bin, sce_sys\ ...); TxK also needs the
                                                  PS Vita firmware: see ..\vita_firmware\

The launcher greys out a game until its files are here, and says what it is looking for.
Space Giraffe and Tempest 4000 are not files: install them in Steam.
"@ | Set-Content -Encoding ascii (Join-Path $dist "game\PUT YOUR GAMES HERE.txt")
    $vf = Join-Path $dist "vita_firmware"
    New-Item -ItemType Directory -Force $vf | Out-Null
@"
PS VITA FIRMWARE (for TxK)
==========================

Put these two files from Sony's PS Vita system software update pages in this folder:

  PSVUPDAT.PUP    the PS Vita system software
  PSP2UPDAT.PUP   the font package

TxK installs them on its first start (it takes a little while; the files can be deleted afterwards).
"@ | Set-Content -Encoding ascii (Join-Path $vf "PUT THE FIRMWARE HERE.txt")
} else {
# The games (hard links: same disk, no second copy).
function Link-Game($src, $name) {
    if (-not (Test-Path $src)) { Write-Warning "missing: $src"; return }
    New-Item -ItemType HardLink -Path (Join-Path $dist "game\$name") -Target $src | Out-Null
}
$jag = Get-ChildItem $repo -Filter "Tempest 2000*.jag" | Select-Object -First 1
if ($jag) { Link-Game $jag.FullName $jag.Name } else { Write-Warning "no Tempest 2000 .jag in the repo folder" }
# Tempest 3000: the compressed image (tools\t3k_compress.py, ~196 MB) when there is one, else the .iso (743 MB)
$t3kNuz = Join-Path $repo "Nuance_067\Tempest 3000.nuz"
if (Test-Path $t3kNuz) { Link-Game $t3kNuz "Tempest 3000.nuz"; $t3kDisc = "Tempest 3000.nuz" }
else { Link-Game (Join-Path $repo "Nuance_067\Tempest 3000.iso") "Tempest 3000.iso"; $t3kDisc = "Tempest 3000.iso" }
Link-Game (Join-Path $repo "tempest.zip") "tempest.zip"
$txk = Join-Path $repo "PCSB00494"
Get-ChildItem $txk -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($repo.Length + 1)
    $to = Join-Path $dist "game\$rel"
    New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
    New-Item -ItemType HardLink -Path $to -Target $_.FullName | Out-Null
}
}

# The Tempest 4000 helper (skips its video-mode dialog; goes into the Steam game's folder, see its README).
$t4k = Join-Path $dist "Tempest 4000 helper"
New-Item -ItemType Directory -Force $t4k | Out-Null
foreach ($f in @("xinput1_4.dll", "xinput1_3.dll")) { Copy-Item (Join-Path $build "t4k\$f") $t4k }
@"
TEMPEST 4000 HELPER - straight into the game at the screen's own resolution and refresh rate
==========================================================================================

Tempest 4000 (Steam) asks for a video mode every time it starts. With these two files next to its program it skips
that window and starts at the desktop's current resolution and refresh rate (also on screens whose own mode the
game leaves off its list, e.g. 4K at 120 Hz). Hold Shift while it starts to get the window back (e.g. for
Configure Keyboard). It writes a small log, t4k_autostart.log, next to the game's program.

The Tempest Collection launcher copies them into place itself each time it starts Tempest 4000 (Windows and the Steam
Deck). On the Deck Proton still needs the launch option below, set once in Steam.

BY HAND (without the launcher): copy xinput1_4.dll and xinput1_3.dll into BOTH of the game's folders:
  <Steam library>\steamapps\common\Tempest 4000\Win10\
  <Steam library>\steamapps\common\Tempest 4000\Win7-8\
  (Steam: right-click Tempest 4000 > Manage > Browse local files.)
  On the Steam Deck the folder is ~/.local/share/Steam/steamapps/common/Tempest 4000/ (or on the SD card's
  steamapps/common), and Proton needs the game to load these files instead of its own: in Steam, Tempest 4000 >
  Properties > General > Launch options:
      WINEDLLOVERRIDES="xinput1_3,xinput1_4=n,b" bash -c 'exec "`${@/Win10/Win7-8}"' -- %command%
  (The bash part starts the game's Windows 7-8 build: under Proton Steam picks the Windows 10 one, whose music is
  in a format Proton doesn't play - no music on the Deck. The Windows 7-8 build has the same music as plain WAVs.)
  The Windows 7-8 build still prefers that format while its folder is there: the Tempest Collection launcher renames
  Win7-8/Assets/Sounds/Tracks_xwma to Tracks_xwma.proton-off on the Deck each time it starts the game (by hand: the
  same rename, in desktop mode). Steam's "verify files" puts the folder back; the next start renames it again.

UNINSTALL: delete the two files (and the launch option). A Steam "verify files" leaves them alone (they are not
the game's files); a game update does too.
"@ | Set-Content -Encoding ascii (Join-Path $t4k "README.txt")

# The Space Giraffe helper: the launcher copies it next to the Steam game and writes the flavour before starting it.
$sg = Join-Path $dist "Space Giraffe helper"
New-Item -ItemType Directory -Force $sg | Out-Null
Copy-Item (Join-Path $build "sg\d3d9.dll") $sg
@"
SPACE GIRAFFE HELPER - straight into the game, borderless full screen at the desktop's resolution and refresh rate
================================================================================================================

The Tempest Collection launcher starts Space Giraffe (Steam) in the flavour shown on its entry (LEFT / RIGHT: Acid or
Nuxx). Before Steam starts the game it copies d3d9.dll from this folder next to SpaceGiraffePC.exe and writes the
flavour into sg_helper.txt there. The helper then:
  - answers the language (English) and flavour windows,
  - shows the game as a borderless window covering the screen, at the desktop's resolution, one frame per refresh,
  - gives the pad a 20% stick deadzone (the game sets none, so its menus jump at the slightest touch).
Other settings in sg_helper.txt (key = value): borderless, vsync, max_fps (e.g. 60), stick_deadzone (percent),
dpi_aware, input_log. Hold Shift while the game starts to get its own windows. Log: sg_helper.log next to the game.

STEAM DECK: the launcher finds the game there too (through the Steam folder Steam gives Proton games) and copies the
helper the same way. Proton must load the helper instead of its own d3d9: set once in Steam, Space Giraffe >
Properties > General > Launch options:
    WINEDLLOVERRIDES="d3d9=n,b" %command%

UNINSTALL: delete d3d9.dll and sg_helper.txt from the game's folder (and the launch option).
"@ | Set-Content -Encoding ascii (Join-Path $sg "README.txt")

# userdata: the play folder's own (kept above) when there was one; then any file it lacks comes from build\userdata
# (settings, T2K EEPROM, save states, TxK saves). Not *.bak, not emulated\ (T3K's log of pictures the native renderer
# couldn't draw: debug states + shots, ~180 MB). Settings files copied from build\ start fullscreen; the play
# folder's own settings are left as the owner set them.
if ($Release) {
    # nothing: each program writes its own settings file with its defaults on first start
} elseif (Test-Path $kept) {
    Remove-Item -Recurse -Force $distUd
    Move-Item $kept $distUd
    Write-Host "userdata: the play folder's own kept (files it lacks added from build\userdata)"
}
$srcUd = (Resolve-Path (Join-Path $build "userdata")).Path
$added = @()
if (-not $Release) {
Get-ChildItem $srcUd -Recurse -File | Where-Object {
    $rel = $_.FullName.Substring($srcUd.Length + 1)
    $_.Name -notlike "*.bak" -and -not ($rel -like "emulated\*")
} | ForEach-Object {
    $dst = Join-Path $distUd $_.FullName.Substring($srcUd.Length + 1)
    if (-not (Test-Path $dst)) {
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item $_.FullName $dst
        $added += Get-Item $dst
    }
}
$added | Where-Object { $_.Name -like "*settings.txt" } | ForEach-Object {
    $t = Get-Content $_.FullName
    $t = $t -replace '^fullscreen\s*=.*$', 'fullscreen = 1'
    # TxK at the Vita's own 960x544 for the Deck (the owner's PC copy may be 4x).
    $t = $t -replace '^vita3k_resolution_multiplier\s*=.*$', 'vita3k_resolution_multiplier = 1'
    Set-Content -Encoding ascii $_.FullName $t
}
}

# Licences: each third-party licence text, and where the source is. Everything is in this project's repository:
# Virtual Jaguar and Nuance vendored (third_party/), MAME and Vita3K as our patches (third_party/patches/, copied here
# too) on the upstream commits tools/get_third_party.sh fetches.
$lic = Join-Path $dist "licences"
New-Item -ItemType Directory -Force $lic | Out-Null
Copy-Item (Join-Path $repo "src\t3k_runtime\THIRD-PARTY.txt") (Join-Path $lic "Tempest 3000 native attribution.txt")
$tp = Join-Path $repo "third_party"
$texts = @(
    @("Virtual Jaguar (libretro) - LICENSE.txt", "virtualjaguar\LICENSE"),
    @("Nuance Resurrection - License.txt", "NuanceResurrection\License.txt"),
    @("MAME (libretro) - COPYING.txt", "mame-libretro\COPYING"),
    @("Vita3K (libretro) - COPYING.txt", "vita3k-libretro\COPYING.txt"),
    @("SDL 3 - LICENSE.txt", "SDL\LICENSE.txt"),
    @("libsndfile - COPYING.txt", "libsndfile\COPYING")
)
foreach ($t in $texts) { Copy-Item (Join-Path $tp $t[1]) (Join-Path $lic $t[0]) }
foreach ($core in @("mame-libretro", "vita3k-libretro")) {
    $pd = Join-Path $lic "patches\$core"
    New-Item -ItemType Directory -Force $pd | Out-Null
    Copy-Item (Join-Path $tp "patches\$core\*.patch") $pd
    # the patches must still be what the core's branch holds (a core change not exported yet would be missing)
    $n = @(git -C (Join-Path $tp $core) rev-list "HEAD" --not --remotes 2>$null).Count
    $have = @(Get-ChildItem (Join-Path $tp "patches\$core") -Filter *.patch).Count
    if ($n -ne $have) { Write-Warning "$core has $n commits of ours but $have patch files: re-export them (git format-patch)" }
}
@"
SOURCE
======

The complete source of this collection - our programs and every change we made to the emulator cores - is in the
Tempest Collection's GitHub repository:

  src\t3k_runtime\                native Tempest 3000 reconstruction and standalone NUZ / Vorbis readers
  src\                            the launcher and the four game programs (t2k, t3k, t81, txk)
  third_party\virtualjaguar\      Virtual Jaguar (GPL-3), with our changes (LLAMASOFT-NOTES.txt)
  third_party\NuanceResurrection\ Nuance Resurrection, with our changes (LLAMASOFT-NOTES.txt)
  third_party\patches\            our changes to MAME and Vita3K (also in patches\ here), applied by
                                  tools\get_third_party.sh to:
                                    MAME (libretro)   https://github.com/libretro/mame at 5f5a0774
                                    Vita3K (libretro) https://github.com/WizzardSK/vita3k-libretro at 29ad0e79
  SDL 3.4.16 and libsndfile 1.2.2 are used unmodified.

The programs that contain GPL code (t2k.exe and the cores) are under the GPL; the licence texts are in this folder.
"@ | Set-Content -Encoding ascii (Join-Path $lic "SOURCE.txt")

if ($Release) {
$gamesText = @"
  TEMPEST (1981 arcade)       MAME core           game\tempest.zip
  TEMPEST 2000 (Jaguar)       Virtual Jaguar      game\<name>.jag  (or .j64)
  TEMPEST 3000 (Nuon)         Native reconstruction              game\<name>.iso  (or .mdf)
  SPACE GIRAFFE               opens Steam (if it is installed there); "Space Giraffe helper\" sets it up
  TxK (PS Vita)               Vita3K              game\PCSB00494\  + the PS Vita firmware in vita_firmware\
  TEMPEST 4000                opens Steam (if it is installed there); "Tempest 4000 helper\" skips its
                              video-mode window and starts at the screen's own mode (see its README)

NO GAMES ARE INCLUDED: bring your own. game\PUT YOUR GAMES HERE.txt says what goes where; the launcher greys out a
game until its files are there and says what it is looking for.

Settings, saves and save states live in userdata\ (made on first start).
Licences and the source of the emulator cores: licences\.
"@
} else {
$gamesText = @"
  TEMPEST (1981 arcade)       MAME core           game\tempest.zip
  TEMPEST 2000 (Jaguar)       Virtual Jaguar      game\Tempest 2000 (1994).jag
  TEMPEST 3000 (Nuon)         Native reconstruction              game\$t3kDisc
  SPACE GIRAFFE               opens Steam (if it is installed there); "Space Giraffe helper\" sets it up
  TxK (PS Vita)               Vita3K              game\PCSB00494\  (firmware already installed in system\vita3k)
  TEMPEST 4000                opens Steam (if it is installed there); "Tempest 4000 helper\" skips its
                              video-mode window and starts at the screen's own mode (see its README)

Settings, saves and save states live in userdata\. This folder is a private copy: do not share it.
"@
}

@"
TEMPEST COLLECTION
==================

Run TempestCollection.exe and pick a game. Each game runs fullscreen on top of the launcher and comes back to it
when you quit (Back / View opens settings in the other games; T3K uses Guide / Esc and keeps Back as NUON).

$gamesText

STEAM DECK (desktop mode, then play in game mode):
 1. Copy this whole folder to the Deck, e.g. /home/deck/Games/TempestCollection
 2. Steam (desktop mode) > Games > Add a Non-Steam Game > Browse > pick TempestCollection.exe
    (set the file type filter to All Files to see .exe files).
 3. In the new entry: Properties > Compatibility > Force the use of a specific Steam Play compatibility tool
    > Proton Experimental (or the newest Proton).
 4. Back in game mode, launch it from your library.
 5. Library art: in the launcher press Y (STEAM ART), then restart Steam (or the Deck) to see it.
    (On Windows, Y on a collection that is not in Steam yet adds it - "ADD TO STEAM": Steam closes for a moment
    and starts again.) By hand instead (steam_art\, desktop mode, on the game's entry in the Steam library):
      grid picture   right-click the game > Manage > Set custom artwork > capsule_600x900.png
      wide picture   in the library's grid, switch the shelf to landscape (or "Recent Games"), right-click >
                     Manage > Set custom artwork > wide_capsule_920x430.png
      banner         open the game's page, right-click the empty banner > Set Custom Background > hero_1920x620.png
      logo           right-click the banner again > Set Custom Logo > logo.png (right-click > Adjust Logo Position)
      icon           Properties > the square icon next to the name > icon_256.png

PAD (each game's menu can remap its buttons)
 Launcher: left / right choose, up / down Space Giraffe's flavour (Acid / Nuxx), A start, Y Steam, B quit.
 Tempest (1981): left stick / d-pad spin, A / RT fire, B / RB / X Superzapper, Y coin, Start start.
 Tempest 2000: A fire, X / RT jump, B / RB Superzapper, Y Option, Start pause, LB camera view.
 Tempest 3000: left stick claw, A fire, B Superzapper, RB jump (Nuon R), X / Y as the Nuon C-left / C-up,
               LB L, LT C-down, RT C-right, Start start, Back NUON. Guide / Esc opens settings.
                Select mouse / trackball in settings: left fire, right jump, middle Superzapper.
 TxK: A Cross, B Circle, X Square, Y Triangle, LB Select, LT hold (L), RT jump (R), Start start, sticks;
      touch the screen (or click) for the touchscreen.
 Back (View) = settings in the other games; Guide / Esc in Tempest 3000 (the game pauses).
"@ | Set-Content -Encoding ascii (Join-Path $dist "README.txt")

$files = Get-ChildItem $dist -Recurse -File
$top = [double](($files | Where-Object { $_.DirectoryName -eq $dist } | Measure-Object Length -Sum).Sum)
Write-Host ("{0,-12} {1,8:N1} MB" -f "(top level)", ($top / 1MB))
foreach ($d in @("game", "system", "userdata")) {
    $sum = [double]((Get-ChildItem (Join-Path $dist $d) -Recurse -File | Measure-Object Length -Sum).Sum)
    Write-Host ("{0,-12} {1,8:N1} MB" -f $d, ($sum / 1MB))
}
$total = [double](($files | Measure-Object Length -Sum).Sum)
Write-Host ("{0,-12} {1,8:N1} MB in {2} files: {3}" -f "total", ($total / 1MB), $files.Count, $dist)

if ($Release) {
    # Safety: nothing of the owner's may be in a public package.
    $bad = $files | Where-Object { $_.Extension -in @(".jag", ".j64", ".rom", ".iso", ".mdf", ".mds", ".nuz", ".chd", ".pup",
                                                       ".state", ".eeprom", ".bak") -or $_.Name -eq "tempest.zip" -or
                                   $_.Name -eq "eboot.bin" -or $_.FullName -match "\\system\\vita3k\\" -or
                                   $_.FullName -match "\\userdata\\" }
    if ($bad) { $bad | ForEach-Object { Write-Host "  NOT ALLOWED: $($_.FullName)" }; Write-Error "game or owner files in the release" }
    $zip = Join-Path $repo ("dist\TempestCollection-{0}.zip" -f (Get-Date -Format "yyyy-MM-dd"))
    . (Join-Path $PSScriptRoot "zip_folder.ps1")
    Write-ZipFolder $dist $zip
    Write-Host ("release zip  {0,8:N1} MB: {1}" -f ((Get-Item $zip).Length / 1MB), $zip)
}

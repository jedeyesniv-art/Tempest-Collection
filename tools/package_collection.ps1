# Builds and assembles the Tempest Collection play folder: dist\TempestCollection\ (the owner's own copy, for their
# own devices - dist\ is not in git and holds the games: never publish it).
#
#   TempestCollection.exe          the launcher (build\tempest.exe)
#   t2k.exe t3k.exe txk.exe t81.exe   the games' programs (the launcher starts them by these names)
#   nuance_libretro.dll vita3k_libretro.dll tempest_libretro.dll   the emulator cores
#   msvcp140*.dll vcruntime140*.dll    the C++ runtime the Vita3K core needs (app-local; the rest are static)
#   system\nuance\                 Nuon BIOS stubs + nuance.cfg (from the Nuance source)
#   system\vita3k\                 the PS Vita firmware, already installed (vs0 / sa0 / os0 ...; no .pup files)
#   game\                          the games: Tempest 2000 .jag, Tempest 3000 .iso, TxK folder, tempest.zip (MAME)
#   userdata\                      the owner's settings, saves and save states (fullscreen on for every program)
#   README.txt                     Steam Deck steps and controls
#
# Game files are hard-linked from the repo (no second copy on this disk; copying the folder elsewhere copies them).
# Usage: powershell -ExecutionPolicy Bypass -File tools\package_collection.ps1 [-SkipBuild]
param([switch]$SkipBuild)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo "build"
$dist = Join-Path $repo "dist\TempestCollection"

if (-not $SkipBuild) {
    $log = Join-Path $build "package_build.log"
    cmd /c "`"$repo\build.bat`" > `"$log`" 2>&1"
    if ($LASTEXITCODE -ne 0 -or (Select-String -Path $log -Pattern "FAILED|error:" -Quiet)) {
        Write-Error "build failed: see $log"
    }
}

if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
foreach ($d in @("", "game", "system", "userdata")) { New-Item -ItemType Directory -Force (Join-Path $dist $d) | Out-Null }

# Programs and cores.
Copy-Item (Join-Path $build "tempest.exe") (Join-Path $dist "TempestCollection.exe")
foreach ($f in @("t2k.exe", "t3k.exe", "txk.exe", "t81.exe", "nuance_libretro.dll", "vita3k_libretro.dll",
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

# System files: the Nuon BIOS stubs, the installed Vita firmware (the core's own data it unpacks again if missing).
Copy-Item -Recurse (Join-Path $build "system\nuance") (Join-Path $dist "system\nuance")
$vsrc = Join-Path $build "system\vita3k"
$vdst = Join-Path $dist "system\vita3k"
if (-not (Test-Path (Join-Path $vsrc "vs0"))) { Write-Error "Vita firmware not installed yet: run build\txk.exe once" }
New-Item -ItemType Directory -Force $vdst | Out-Null
Get-ChildItem $vsrc | Where-Object { $_.Extension -ne ".pup" -and $_.Name -notin @("log", "cache") } |
    ForEach-Object { Copy-Item -Recurse $_.FullName (Join-Path $vdst $_.Name) }

# The games (hard links: same disk, no second copy).
function Link-Game($src, $name) {
    if (-not (Test-Path $src)) { Write-Warning "missing: $src"; return }
    New-Item -ItemType HardLink -Path (Join-Path $dist "game\$name") -Target $src | Out-Null
}
$jag = Get-ChildItem $repo -Filter "Tempest 2000*.jag" | Select-Object -First 1
if ($jag) { Link-Game $jag.FullName $jag.Name } else { Write-Warning "no Tempest 2000 .jag in the repo folder" }
Link-Game (Join-Path $repo "Nuance_067\Tempest 3000.iso") "Tempest 3000.iso"
Link-Game (Join-Path $repo "tempest.zip") "tempest.zip"
$txk = Join-Path $repo "PCSB00494"
Get-ChildItem $txk -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($repo.Length + 1)
    $to = Join-Path $dist "game\$rel"
    New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
    New-Item -ItemType HardLink -Path $to -Target $_.FullName | Out-Null
}

# The owner's userdata (settings, T2K EEPROM, save states, TxK saves), every program set to start fullscreen.
Get-ChildItem (Join-Path $build "userdata") | Where-Object { $_.Name -notlike "*.bak" } |
    ForEach-Object { Copy-Item -Recurse $_.FullName (Join-Path $dist "userdata\$($_.Name)") }
Get-ChildItem (Join-Path $dist "userdata") -Filter "*settings.txt" | ForEach-Object {
    $t = Get-Content $_.FullName
    $t = $t -replace '^fullscreen\s*=.*$', 'fullscreen = 1'
    Set-Content -Encoding ascii $_.FullName $t
}

@"
TEMPEST COLLECTION
==================

Run TempestCollection.exe and pick a game. Each game runs fullscreen on top of the launcher and comes back to it
when you quit (Back / View on the pad opens a game's settings menu, which has "Return to launcher").

  TEMPEST (1981 arcade)       MAME core           game\tempest.zip
  TEMPEST 2000 (Jaguar)       Virtual Jaguar      game\Tempest 2000 (1994).jag
  TEMPEST 3000 (Nuon)         Nuance              game\Tempest 3000.iso
  TxK (PS Vita)               Vita3K              game\PCSB00494\  (firmware already installed in system\vita3k)
  TEMPEST 4000                opens Steam (if it is installed there)

Settings, saves and save states live in userdata\. This folder is a private copy: do not share it.

STEAM DECK (desktop mode, then play in game mode):
 1. Copy this whole folder to the Deck, e.g. /home/deck/Games/TempestCollection
 2. Steam (desktop mode) > Games > Add a Non-Steam Game > Browse > pick TempestCollection.exe
    (set the file type filter to All Files to see .exe files).
 3. In the new entry: Properties > Compatibility > Force the use of a specific Steam Play compatibility tool
    > Proton Experimental (or the newest Proton).
 4. Back in game mode, launch it from your library.

PAD (each game's menu can remap its buttons)
 Launcher: up / down choose, A start, B quit.
 Tempest (1981): left stick / d-pad spin, A / RT fire, B / RB / X Superzapper, Y coin, Start start.
 Tempest 2000: A fire, X / RT jump, B / RB Superzapper, Y Option, Start pause, LB camera view.
 Tempest 3000: left stick claw, A / B / X / Y as the Nuon A / B / C-left / C-up, LB / RB L / R, LT / RT C-down /
               C-right, Start start.
 TxK: A Cross, B Circle, X Square, Y Triangle, LB / RB L / R, LT Select, Start start, sticks.
 Back (View) = the game's settings menu (the game pauses).
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

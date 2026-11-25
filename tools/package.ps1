# Builds and assembles a portable test build: dist\Tempest2000\ (+ dist\Tempest2000-test.zip).
#   Tempest2000.exe   static build: no DLLs
#   <the cartridge image>   the owner's own copy, for their own devices (dist\ is not in git; never publish this)
#   userdata\settings.txt   starts fullscreen
#   README.txt
# Usage: powershell -ExecutionPolicy Bypass -File tools\package.ps1 [-SkipBuild]
param([switch]$SkipBuild)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $repo "dist\Tempest2000"
$zip = Join-Path $repo "dist\Tempest2000-test.zip"

if (-not $SkipBuild) {
    $log = Join-Path $repo "build\package_build.log"
    cmd /c "`"$repo\build.bat`" > `"$log`" 2>&1"
    if ($LASTEXITCODE -ne 0 -or (Select-String -Path $log -Pattern "FAILED|error:" -Quiet)) {
        Write-Error "build failed: see $log"
    }
}

if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force (Join-Path $dist "userdata") | Out-Null
Copy-Item (Join-Path $repo "build\t2k.exe") (Join-Path $dist "Tempest2000.exe")
$rom = Get-ChildItem $repo -Filter "Tempest 2000*.jag" | Select-Object -First 1
if ($rom) { Copy-Item $rom.FullName $dist } else { Write-Warning "no Tempest 2000 .jag next to the repo: add one to the folder" }

@"
# Tempest 2000 test build settings (the menu - Back / Esc - changes and saves these).
fullscreen = 1
"@ | Set-Content -Encoding ascii (Join-Path $dist "userdata\settings.txt")

@"
TEMPEST 2000 - remastered test build
====================================

Windows: run Tempest2000.exe. Saves, settings and save states go in the userdata folder next to it.
The cartridge image (.jag) must be in this folder.

STEAM DECK (desktop mode, then play in game mode):
 1. Copy this whole folder to the Deck, e.g. /home/deck/Games/Tempest2000
 2. Steam (desktop mode) > Games > Add a Non-Steam Game > Browse > pick Tempest2000.exe
    (set the file type filter to All Files to see .exe files).
 3. In the new entry: Properties > Compatibility > Force the use of a specific Steam Play compatibility tool
    > Proton Experimental (or the newest Proton).
 4. Back in game mode, launch it from your library. It starts fullscreen.

CONTROLS (pad)
 A fire, X / RT jump, B / RB Superzapper, Y Option, Start pause, LB camera view,
 left stick / d-pad move the claw. Back (View) = the settings menu (game paused).
 Keyboard: arrows, Z fire, X jump, C zap, O Option, P / Enter pause, Esc menu, F9 CRT look, F11 fullscreen,
 number keys = the Jaguar keypad (- is *, = is #).

The settings menu has DISPLAY (remaster, CRT+ / CRT++, pixel art, melt), CHEATS and save states.
"Original mode" turns every improvement off.
"@ | Set-Content -Encoding ascii (Join-Path $dist "README.txt")

. (Join-Path $PSScriptRoot "zip_folder.ps1")
Write-ZipFolder $dist $zip
Get-ChildItem $dist -Recurse | Select-Object FullName, Length | Format-Table -AutoSize
Write-Host "zip: $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"

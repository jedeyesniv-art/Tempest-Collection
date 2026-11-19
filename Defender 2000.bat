@echo off
rem Defender 2000 standalone: build\d2k.exe (the same program as t2k.exe, renamed by the build; by its name it
rem plays Defender 2000 with its own settings menu, controls and saves: build\userdata\d2k_settings.txt,
rem defender2000.eeprom, states\d2k_slotN.state).
rem Keys: Esc or pad Back = settings menu, F11 fullscreen, F8 native / emulated picture, F7 smooth motion,
rem F6 wide screen, F4 high-res art (buildk_art, toolsk_art_pack.py). Pad: A fire, B / RB smart bomb, X lightning laser (Plus), RT thrust, LB / LT reverse, Y Option,
rem Start pause (remap in the menu).
cd /d "%~dp0"
if not exist "build\d2k.exe" (
  echo build\d2k.exe is missing - run build.bat first.
  pause
  exit /b 1
)
if not exist "Defender 2000 (1996).jag" (
  echo "Defender 2000 (1996).jag" is missing from %~dp0
  pause
  exit /b 1
)
start "" "%~dp0build\d2k.exe" "%~dp0Defender 2000 (1996).jag"

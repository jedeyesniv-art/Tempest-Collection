@echo off
rem Defender 2000 with the native renderer (build\t2k.exe hosts it for now).
rem Settings / saves / states: build\userdata_d2k\ (separate from Tempest 2000's).
rem Keys: F11 fullscreen, F8 native / emulated picture, F7 smooth motion on / off, F6 wide screen on / off, Esc or pad Back = menu.
cd /d "%~dp0"
if not exist "build\t2k.exe" (
  echo build\t2k.exe is missing - run build.bat first.
  pause
  exit /b 1
)
if not exist "Defender 2000 (1996).jag" (
  echo "Defender 2000 (1996).jag" is missing from %~dp0
  pause
  exit /b 1
)
start "" "%~dp0build\t2k.exe" "%~dp0Defender 2000 (1996).jag" --userdata="%~dp0build\userdata_d2k"

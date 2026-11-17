@echo off
rem Builds the Space Giraffe helper (src\sg\d3d9_proxy.cpp): 32-bit, as build\sg\d3d9.dll. Copy it next to
rem SpaceGiraffePC.exe (Steam app 27800), with sg_helper.txt for the settings.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" (
  call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
) else (
  call "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
)
set "SRC=%~dp0..\src\sg"
set "OUT=%~dp0..\build\sg"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /O2 /MT /LD /EHsc /D_CRT_SECURE_NO_WARNINGS "%SRC%\d3d9_proxy.cpp" ^
   /Fo"%OUT%\d3d9.obj" /Fe"%OUT%\d3d9.dll" /link /DEF:"%SRC%\d3d9_proxy.def" user32.lib winmm.lib || exit /b 1

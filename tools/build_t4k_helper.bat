@echo off
rem Builds the Tempest 4000 helper (src\t4k\xinput_proxy.cpp): 32-bit, as build\t4k\xinput1_4.dll (Win10 build of the
rem game) and build\t4k\xinput1_3.dll (Win7-8 build). Copy both next to Tempest4000.exe (Win10 and Win7-8 folders).
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
set "SRC=%~dp0..\src\t4k"
set "OUT=%~dp0..\build\t4k"
if not exist "%OUT%" mkdir "%OUT%"
for %%N in (xinput1_4 xinput1_3) do (
  cl /nologo /O2 /MT /LD /EHsc /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS "%SRC%\xinput_proxy.cpp" ^
     /Fo"%OUT%\%%N.obj" /Fe"%OUT%\%%N.dll" /link /DEF:"%SRC%\xinput_proxy.def" user32.lib || exit /b 1
)

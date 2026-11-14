@echo off
rem The Nuance core with debug info (RelWithDebInfo) -> build_nuance_dbg\nuance_libretro.dll, for lldb backtraces.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin;%PATH%"
set "SRC=%~dp0..\third_party\NuanceResurrection"
set "OUT=%~dp0..\build_nuance_dbg"
if not exist "%OUT%\build.ninja" cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DBUILD_LIBRETRO=ON || exit /b 1
cmake --build "%OUT%" %*

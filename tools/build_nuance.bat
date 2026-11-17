@echo off
rem Builds the Nuance libretro core (third_party\NuanceResurrection, branch llamasoft-nuon)
rem -> build_nuance\nuance_libretro.dll (Release, clang, ThinLTO + lld, Ninja) inside the VS x64 environment.
rem Default: NO AOT modules (2026-11-17: they are translations of the game's code, so never in a release; the JIT
rem runs the game ~15% slower than AOT, well inside the budget - Deck verified by the owner). Delete build_nuance\
rem after changing the options here.
rem "build_nuance.bat aot" -> build_nuance_aot\ with the AOT modules (build_aot\*.cpp, generated from the game by
rem tools\t3k_aot*.py; not in git; ThinLTO inlines the interpreter handlers into them) - for local experiments only.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin;%PATH%"
set "SRC=%~dp0..\third_party\NuanceResurrection"
set "OUT=%~dp0..\build_nuance"
set "AOT="
if /i "%~1"=="aot" (
  set "OUT=%~dp0..\build_nuance_aot"
  set "AOT=-DNUANCE_AOT_DIR=%~dp0..\build_aot"
  shift
)
if not exist "%OUT%\build.ninja" cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DBUILD_LIBRETRO=ON %AOT% "-DCMAKE_CXX_FLAGS=-flto=thin" "-DCMAKE_C_FLAGS=-flto=thin" "-DCMAKE_SHARED_LINKER_FLAGS=-flto=thin" -DCMAKE_LINKER_TYPE=LLD || exit /b 1
cmake --build "%OUT%"

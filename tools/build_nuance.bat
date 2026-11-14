@echo off
rem Builds the Nuance libretro core (third_party\NuanceResurrection, branch llamasoft-nuon)
rem -> build_nuance\nuance_libretro.dll (Release, clang, ThinLTO + lld, Ninja) inside the VS x64 environment.
rem ThinLTO matters: it inlines the interpreter handlers into the AOT modules (build_aot\*.cpp, generated from the
rem game by tools\t3k_aot*.py; not in git). Delete build_nuance\ after changing the options here.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin;%PATH%"
set "SRC=%~dp0..\third_party\NuanceResurrection"
set "OUT=%~dp0..\build_nuance"
if not exist "%OUT%\build.ninja" cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DBUILD_LIBRETRO=ON "-DNUANCE_AOT_DIR=%~dp0..\build_aot" "-DCMAKE_CXX_FLAGS=-flto=thin" "-DCMAKE_C_FLAGS=-flto=thin" "-DCMAKE_SHARED_LINKER_FLAGS=-flto=thin" -DCMAKE_LINKER_TYPE=LLD || exit /b 1
cmake --build "%OUT%" %*

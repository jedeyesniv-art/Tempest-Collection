@echo off
rem Builds the Vita3K libretro core (third_party\vita3k-libretro, branch libretro) -> build_vita3k\Release\vita3k_libretro.dll
rem the way upstream's Windows CI does: MSVC, preset windows-ninja (Ninja Multi-Config), BUILD_LIBRETRO=ON.
rem Configure builds a static OpenSSL from source, which needs a native Perl: the portable Strawberry Perl in
rem third_party\strawberry (Git's perl is rejected). It also downloads a prebuilt FFmpeg and the curl source.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "SRC=%~dp0..\third_party\vita3k-libretro"
set "OUT=%~dp0..\build_vita3k"
set "PERL=%~dp0..\third_party\strawberry\perl\bin\perl.exe"
set "CMAKE_POLICY_VERSION_MINIMUM=3.5"
if not exist "%OUT%\build.ninja" cmake -S "%SRC%" -B "%OUT%" -G "Ninja Multi-Config" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DBUILD_LIBRETRO=ON "-DPERL_EXECUTABLE=%PERL%" || exit /b 1
cmake --build "%OUT%" --config Release --target vita3k_libretro %*

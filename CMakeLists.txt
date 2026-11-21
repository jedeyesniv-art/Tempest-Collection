cmake_minimum_required(VERSION 3.24)
project(LlamasoftJaguar VERSION 0.1.0 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# SDL3, static (same source tree as the ST ports: 3.4.16).
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)

# The Virtual Jaguar core (our local branch llamasoft-jaguar), built by its own CMake module.
set(VJ_LTO_ENABLED OFF)
function(vj_optimise target)
endfunction()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/third_party/virtualjaguar/app/cmake")
include(VJCore)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  target_compile_options(vj_core PRIVATE -w)
  target_compile_options(vj_libchdr PRIVATE -w)
endif()

add_executable(t2k WIN32 src/main.cpp src/host.cpp src/settings.cpp src/volume.cpp src/runlog.cpp src/t2k.cpp src/menu.cpp src/pixfont.cpp src/t2k_menu.cpp src/d2k_menu.cpp src/controls.cpp src/traps.cpp src/gpulog.cpp src/jagsurvey.cpp src/d2k.cpp src/d2k_bonus.cpp src/d2k_stars.cpp src/remaster.cpp src/perf.cpp src/xbr.cpp src/crt.cpp src/crt_menu.cpp res/t2k.rc)
target_link_libraries(t2k PRIVATE vj_core SDL3::SDL3-static)
target_compile_definitions(t2k PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
if(WIN32)
  target_link_libraries(t2k PRIVATE ws2_32 iphlpapi winmm)
endif()
# Defender 2000 standalone: the same program as d2k.exe (by its name it looks for the Defender 2000 cartridge and
# keeps its own settings / menu / controls; src/main.cpp g_defender).
add_custom_command(TARGET t2k POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:t2k> $<TARGET_FILE_DIR:t2k>/d2k.exe)

# Tempest Collection launcher (tempest.exe): picks a game and starts its program (t2k.exe, t3k.exe) or Steam.
add_executable(tempest WIN32 src/launcher.cpp src/launcher_view.cpp src/pixfont.cpp src/menu.cpp src/controls.cpp src/launcher_sound.cpp src/steam_shortcut.cpp src/settings.cpp src/t3k_compress.cpp third_party/NuanceResurrection/src/stb_vorbis.c res/t2k.rc)
target_include_directories(tempest PRIVATE third_party/NuanceResurrection/src)   # stb_vorbis.c (the compression's check)
# libsndfile (LGPL) for the T3K disc compression's Vorbis encoder, loaded at run time: next to the launcher
add_custom_command(TARGET tempest POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/libsndfile/libsndfile_x64.dll"
          "${CMAKE_SOURCE_DIR}/third_party/libsndfile/COPYING" $<TARGET_FILE_DIR:tempest>)
target_link_libraries(tempest PRIVATE SDL3::SDL3-static)
target_compile_definitions(tempest PRIVATE _CRT_SECURE_NO_WARNINGS)
if(WIN32)
  target_link_libraries(tempest PRIVATE winmm advapi32)
endif()

# Tempest 3000 (Nuon): the Nuance libretro core is a separate DLL (tools\build_nuance.bat -> build_nuance\), loaded at
# run time; its BIOS stubs (from the Nuance source) and our nuance.cfg go to system\nuance next to the exe.
add_executable(t3k WIN32 src/t3k_main.cpp src/volume.cpp src/t3k_menu.cpp src/t3k_native.cpp src/t3k_scores.cpp src/nuon_host.cpp src/settings.cpp src/runlog.cpp src/menu.cpp src/pixfont.cpp src/controls.cpp src/crt.cpp src/crt_menu.cpp)
target_link_libraries(t3k PRIVATE SDL3::SDL3-static opengl32)
target_compile_definitions(t3k PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
if(WIN32)
  target_link_libraries(t3k PRIVATE winmm)
endif()
set(NUANCE_SRC "${CMAKE_SOURCE_DIR}/third_party/NuanceResurrection")
# A target of its own (not POST_BUILD): the DLL changes without t3k relinking.
add_custom_target(t3k_runtime ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/system/nuance"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NUANCE_SRC}/bios.cof" "${NUANCE_SRC}/minibios.cof"
          "${NUANCE_SRC}/minibiosX.cof" "${CMAKE_SOURCE_DIR}/res/nuance/nuance.cfg" "${CMAKE_BINARY_DIR}/system/nuance"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/build_nuance/nuance_libretro.dll"
          "${CMAKE_BINARY_DIR}"
  DEPENDS t3k)

# TxK (PS Vita): the Vita3K libretro core is a separate DLL (tools\build_vita3k.bat -> build_vita3k\bin\Release\),
# loaded at run time. Firmware comes from vita_firmware\ (txk.exe links it into system\vita3k\ on first start).
add_executable(txk WIN32 src/txk_main.cpp src/volume.cpp src/txk_menu.cpp src/lr_host.cpp src/settings.cpp src/runlog.cpp src/menu.cpp src/pixfont.cpp src/controls.cpp)
target_link_libraries(txk PRIVATE SDL3::SDL3-static opengl32)
target_compile_definitions(txk PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
if(WIN32)
  target_link_libraries(txk PRIVATE winmm)
endif()
add_custom_target(txk_runtime ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/build_vita3k/bin/Release/vita3k_libretro.dll"
          "${CMAKE_BINARY_DIR}"
  DEPENDS txk)

# Tempest (1981 arcade): MAME's libretro core with the Tempest driver only (tools/build_mame.sh, MinGW gcc from the
# portable Strawberry Perl -> third_party/mame-libretro/tempest_libretro.dll), loaded at run time.
add_executable(t81 WIN32 src/t81_main.cpp src/volume.cpp src/t81_menu.cpp src/t81_vectors.cpp src/lr_host.cpp src/settings.cpp src/runlog.cpp src/menu.cpp src/pixfont.cpp src/controls.cpp src/crt.cpp src/crt_menu.cpp)
target_link_libraries(t81 PRIVATE SDL3::SDL3-static opengl32)
target_compile_definitions(t81 PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
if(WIN32)
  target_link_libraries(t81 PRIVATE winmm)
endif()
add_custom_target(t81_runtime ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/mame-libretro/tempest_libretro.dll"
          "${CMAKE_BINARY_DIR}"
  DEPENDS t81)

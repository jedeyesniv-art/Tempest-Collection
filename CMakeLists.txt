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

add_executable(t2k WIN32 src/main.cpp src/host.cpp src/settings.cpp src/t2k.cpp src/menu.cpp src/controls.cpp src/traps.cpp src/gpulog.cpp src/remaster.cpp src/xbr.cpp src/crt.cpp res/t2k.rc)
target_link_libraries(t2k PRIVATE vj_core SDL3::SDL3-static)
target_compile_definitions(t2k PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
if(WIN32)
  target_link_libraries(t2k PRIVATE ws2_32 iphlpapi winmm)
endif()

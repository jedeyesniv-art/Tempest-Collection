# The reconstructed game is ordinary native C++ and uses the collection's GPU
# renderer. The disc supplies original data; no Nuance DLL or BIOS is loaded.
add_executable(t3k WIN32 src/t3k_main.cpp
  src/t3k_runtime/native_frame_integration.cpp src/t3k_runtime/native_frame_platform.cpp
  src/t3k_runtime/native_renderer_bridge.cpp src/t3k_runtime/disc_image.cpp
  src/t3k_runtime/stb_vorbis.c
  src/t3k_native.cpp src/t3k_scores.cpp src/t3k_menu.cpp
  src/volume.cpp src/runlog.cpp src/settings.cpp src/menu.cpp src/pixfont.cpp
  src/controls.cpp src/pads.cpp src/crt.cpp src/crt_menu.cpp res/t2k.rc)
target_compile_definitions(t3k PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
target_link_libraries(t3k PRIVATE SDL3::SDL3-static)
if(WIN32)
  target_link_libraries(t3k PRIVATE winmm)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(t3k_checkpoint_test tests/t3k_checkpoint.cpp)
  target_include_directories(t3k_checkpoint_test PRIVATE src)
  target_compile_definitions(t3k_checkpoint_test PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
  target_link_libraries(t3k_checkpoint_test PRIVATE SDL3-static)
  add_test(NAME t3k_checkpoint COMMAND t3k_checkpoint_test)
endif()

option(TEMPEST_T3K_REFERENCE_TESTS "Build external original-reference replay tools (no game data included)" OFF)
if(TEMPEST_T3K_REFERENCE_TESTS)
  add_executable(t3k_idle_benchmark tests/native_idle_benchmark.cpp src/settings.cpp
    src/t3k_runtime/native_renderer_bridge.cpp src/t3k_runtime/native_frame_integration.cpp
    src/t3k_runtime/native_frame_platform.cpp src/t3k_runtime/disc_image.cpp
    src/t3k_runtime/stb_vorbis.c src/t3k_native.cpp)
  target_include_directories(t3k_idle_benchmark PRIVATE src/t3k_runtime)
  target_compile_definitions(t3k_idle_benchmark PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(t3k_idle_benchmark PRIVATE SDL3::SDL3-static)
  foreach(kind jump whole_frame)
    add_executable(t3k_${kind}_replay tests/native_${kind}_replay.cpp
      src/t3k_runtime/native_renderer_bridge.cpp src/t3k_runtime/native_frame_integration.cpp
      src/t3k_runtime/native_frame_platform.cpp src/t3k_runtime/disc_image.cpp
      src/t3k_runtime/stb_vorbis.c src/t3k_native.cpp)
    target_include_directories(t3k_${kind}_replay PRIVATE src/t3k_runtime)
    target_compile_definitions(t3k_${kind}_replay PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(t3k_${kind}_replay PRIVATE SDL3::SDL3-static)
  endforeach()
endif()

if(BUILD_TESTING)
  add_executable(t3k_controls_test tests/t3k_controls.cpp src/t3k_menu.cpp src/settings.cpp
    src/menu.cpp src/controls.cpp src/pads.cpp src/pixfont.cpp src/crt.cpp src/crt_menu.cpp)
  target_include_directories(t3k_controls_test PRIVATE src)
  target_compile_definitions(t3k_controls_test PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(t3k_controls_test PRIVATE SDL3::SDL3-static)
  add_test(NAME t3k_controls COMMAND t3k_controls_test)
endif()

# The legacy cache name is retained for existing build configurations.
# Normal builds now ship the validated scene profile and framerate selector.
option(TEMPEST_T3K_TIMING_RESEARCH "Native scene timing and framerate selection (no interpolation)" ON)
if(TEMPEST_T3K_TIMING_RESEARCH)
  target_compile_definitions(t3k PRIVATE TEMPEST_T3K_TIMING_RESEARCH=1)
endif()

if(TEMPEST_T3K_REFERENCE_TESTS)
  add_executable(t3k_timing_render_test tests/t3k_timing_render.cpp
    src/t3k_runtime/native_renderer_bridge.cpp src/t3k_native.cpp)
  target_include_directories(t3k_timing_render_test PRIVATE src/t3k_runtime)
  target_compile_definitions(t3k_timing_render_test PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(t3k_timing_render_test PRIVATE SDL3::SDL3-static)
endif()

if(BUILD_TESTING)
  add_executable(t3k_fractional_motion_test tests/t3k_fractional_motion.cpp)
  target_include_directories(t3k_fractional_motion_test PRIVATE src)
  add_test(NAME t3k_fractional_motion COMMAND t3k_fractional_motion_test)
  add_executable(t3k_cycle_contracts_test tests/t3k_cycle_contracts.cpp)
  target_include_directories(t3k_cycle_contracts_test PRIVATE src)
  target_compile_definitions(t3k_cycle_contracts_test PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(t3k_cycle_contracts_test PRIVATE SDL3::SDL3-static)
  add_test(NAME t3k_cycle_contracts COMMAND t3k_cycle_contracts_test)
endif()

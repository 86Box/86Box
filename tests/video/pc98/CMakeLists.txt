cmake_minimum_required(VERSION 3.16)
# Also usable standalone, without Qt or GoogleTest:
# cmake -S tests/video/pc98 -B build/pc98-tests
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pc98_graphics_tests C)
    enable_testing()
endif()
add_executable(pc98_graphics_tests
    ../pc98_graphics_test.c
    ../../../src/video/vid_pc98.c
    ../../../src/video/vid_upd7220.c
)
target_include_directories(pc98_graphics_tests PRIVATE ../../../src/include)
target_compile_features(pc98_graphics_tests PRIVATE c_std_11)
add_test(NAME pc98_graphics COMMAND pc98_graphics_tests)

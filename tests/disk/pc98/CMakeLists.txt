cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pc98_disk_tests C)
    enable_testing()
endif()
add_executable(pc98_disk_tests ../pc98_disk_test.c
    ../../../src/disk/pc98_hdi.c
    ../../../src/disk/pc98_hdi_import.c
    ../../../src/disk/pc98_sasi.c)
target_include_directories(pc98_disk_tests PRIVATE ../../../src/include)
target_compile_features(pc98_disk_tests PRIVATE c_std_11)
target_compile_options(pc98_disk_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Werror>)
add_test(NAME pc98_disk COMMAND pc98_disk_tests)

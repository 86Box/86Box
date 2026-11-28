cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pc98_mouse_tests C)
    enable_testing()
endif()
add_executable(pc98_mouse_tests ../pc98_mouse_test.c
    ../../../src/device/pc98_mouse.c ../../../src/device/pc98_ppi8255.c
    ../../../src/device/lpt.c ../../../src/utils/fifo.c)
target_include_directories(pc98_mouse_tests PRIVATE ../../../src/include ../../../src/cpu)
target_compile_features(pc98_mouse_tests PRIVATE c_std_11)
set_property(TARGET pc98_mouse_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_compile_definitions(pc98_mouse_tests PRIVATE "$<$<CONFIG:Release>:RELEASE_BUILD>")
target_compile_options(pc98_mouse_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>)
target_link_options(pc98_mouse_tests PRIVATE
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
    $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>)
add_test(NAME pc98_mouse COMMAND pc98_mouse_tests)

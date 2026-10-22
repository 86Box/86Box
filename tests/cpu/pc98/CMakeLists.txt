cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pc98_cpu_bus_tests C)
    enable_testing()
endif()
add_executable(pc98_cpu_bus_tests ../pc98_bus_test.c
    ../../../src/timer.c ../../../src/cpu/pc98_wait.c)
target_include_directories(pc98_cpu_bus_tests PRIVATE ../../../src/include ../../../src/cpu)
target_compile_features(pc98_cpu_bus_tests PRIVATE c_std_11)
set_property(TARGET pc98_cpu_bus_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_compile_options(pc98_cpu_bus_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>)
target_link_options(pc98_cpu_bus_tests PRIVATE
    $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
    $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>)
add_test(NAME pc98_cpu_bus COMMAND pc98_cpu_bus_tests)

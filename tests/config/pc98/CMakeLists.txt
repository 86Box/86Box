cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pc98_config_tests VERSION 0.0.0 LANGUAGES C)
    enable_testing()
    set(EMU_BUILD_NUM 0)
    configure_file(../../../src/include/86box/version.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/86box/version.h)
endif()
add_executable(pc98_config_tests ../pc98_sasi_config_test.c
    ../../../src/utils/ini.c
    ../../../src/disk/hdd.c)
target_include_directories(pc98_config_tests PRIVATE
    ../../../src/include ../../../src/cpu
    ${PROJECT_BINARY_DIR}/src/include ${CMAKE_CURRENT_BINARY_DIR}/include)
target_compile_features(pc98_config_tests PRIVATE c_std_11)
set_property(TARGET pc98_config_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_compile_options(pc98_config_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>)
target_link_options(pc98_config_tests PRIVATE
    $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
    $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>)
add_test(NAME pc98_sasi_config COMMAND pc98_config_tests
    ${CMAKE_CURRENT_BINARY_DIR}/sasi-round-trip.cfg)

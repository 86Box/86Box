cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pc98_dma_tests C)
    enable_testing()
endif()
add_executable(pc98_dma_tests ../pc98_dma_test.c)
target_include_directories(pc98_dma_tests PRIVATE ../../../src/include ../../../src/cpu)
target_compile_features(pc98_dma_tests PRIVATE c_std_11)
target_compile_definitions(pc98_dma_tests PRIVATE "$<$<CONFIG:Release>:RELEASE_BUILD>")
set_property(TARGET pc98_dma_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_compile_options(pc98_dma_tests PRIVATE
    $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>)
target_link_options(pc98_dma_tests PRIVATE
    $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
    $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>)
add_test(NAME pc98_dma COMMAND pc98_dma_tests)

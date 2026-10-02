cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pc98_floppy_tests C)
    enable_testing()
endif()
add_executable(pc98_floppy_tests
    ../pc98_floppy_test.c
    ../../../src/floppy/fdd_img.c
    ../../../src/floppy/fdd_86f.c
    ../../../src/utils/fifo.c
    ../../../src/utils/crc.c
    ../../../src/device/pc98_ppi8255.c
    ../../../src/device/pc98_i8251.c
)
target_include_directories(pc98_floppy_tests PRIVATE
    ../../../src/include ../../../src/cpu)
target_compile_features(pc98_floppy_tests PRIVATE c_std_11)
# The harness includes the production motherboard, which also contains machine
# registration. Eliminate its unused host/device setup before linking adapters.
set_property(TARGET pc98_floppy_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_compile_definitions(pc98_floppy_tests PRIVATE DISABLE_FDD_AUDIO
    "$<$<CONFIG:Release>:RELEASE_BUILD>")
# Optional overrides let the same regressions run against saved old sources.
foreach(source PC98_FDC_SOURCE PC98_FDD_SOURCE PC98_BOARD_SOURCE)
    if(DEFINED ${source})
        target_compile_definitions(pc98_floppy_tests PRIVATE ${source}="${${source}}")
    endif()
endforeach()
target_compile_options(pc98_floppy_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>)
target_link_options(pc98_floppy_tests PRIVATE
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
    $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>)
add_test(NAME pc98_floppy COMMAND pc98_floppy_tests)

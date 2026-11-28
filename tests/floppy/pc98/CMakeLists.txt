cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pc98_floppy_tests C)
    enable_testing()
endif()
set(pc98_floppy_sources
    ../../../src/floppy/fdd_img.c
    ../../../src/floppy/fdd_86f.c
    ../../../src/utils/fifo.c
    ../../../src/utils/crc.c
    ../../../src/device/pc98_ppi8255.c
    ../../../src/device/pc98_i8251.c
    ../../../src/device/pc98_mouse.c
)
add_executable(pc98_floppy_tests ../pc98_floppy_test.c ${pc98_floppy_sources})
add_executable(pc98_floppy_dma_tests ../pc98_floppy_dma_test.c ${pc98_floppy_sources})
foreach(target pc98_floppy_tests pc98_floppy_dma_tests)
target_include_directories(${target} PRIVATE
    ../../../src/include ../../../src/cpu)
target_compile_features(${target} PRIVATE c_std_11)
# The harness includes the production motherboard, which also contains machine
# registration. Eliminate its unused host/device setup before linking adapters.
set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_compile_definitions(${target} PRIVATE DISABLE_FDD_AUDIO
    "$<$<CONFIG:Release>:RELEASE_BUILD>")
# Optional overrides let the same regressions run against saved old sources.
foreach(source PC98_FDC_SOURCE PC98_FDD_SOURCE PC98_BOARD_SOURCE)
    if(DEFINED ${source})
        target_compile_definitions(${target} PRIVATE ${source}="${${source}}")
    endif()
endforeach()
target_compile_options(${target} PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>)
target_link_options(${target} PRIVATE
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
    $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>)
endforeach()
add_test(NAME pc98_floppy COMMAND pc98_floppy_tests)
add_test(NAME pc98_floppy_dma COMMAND pc98_floppy_dma_tests)

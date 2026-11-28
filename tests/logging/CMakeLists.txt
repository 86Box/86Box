cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(logging_tests VERSION 0.0.0 LANGUAGES C)
    enable_testing()
    set(EMU_BUILD_NUM 0)
    set(EMU_COPYRIGHT_YEAR 2026)
    configure_file(../../src/include/86box/version.h.in include/86box/version.h)
endif()

foreach(policy debug release)
    add_executable(logging_${policy}_tests logging_test.c
        ../../src/86box.c ../../src/utils/log.c)
    target_include_directories(logging_${policy}_tests PRIVATE
        ../../src/include ../../src/cpu "${CMAKE_CURRENT_BINARY_DIR}/include"
        "${PROJECT_BINARY_DIR}/src/include")
    target_compile_features(logging_${policy}_tests PRIVATE c_std_11)
    set_property(TARGET logging_${policy}_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    target_compile_options(logging_${policy}_tests PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
        # Discard unrelated emulator entry points when linking host adapters.
        $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>)
    target_link_options(logging_${policy}_tests PRIVATE
        $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-O1>
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>)
    if(policy STREQUAL release)
        target_compile_definitions(logging_${policy}_tests PRIVATE RELEASE_BUILD)
    else()
        # Check debug behavior even when the parent project is a Release build.
        target_compile_options(logging_${policy}_tests PRIVATE
            $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-URELEASE_BUILD>)
    endif()
    add_test(NAME logging_${policy} COMMAND logging_${policy}_tests)
endforeach()

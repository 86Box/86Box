if(BUILD_TESTING)
    add_library(fdc_read_id_engine OBJECT ../../src/floppy/fdd_86f.c)
    target_include_directories(fdc_read_id_engine PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(fdc_read_id_engine PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    )

    add_executable(fdc_read_id_tests
        fdc_read_id_test.cpp
        $<TARGET_OBJECTS:fdc_read_id_engine>
    )
    target_include_directories(fdc_read_id_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(fdc_read_id_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    )
    target_link_options(fdc_read_id_tests PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<CXX_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    )
    target_link_libraries(fdc_read_id_tests PRIVATE GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(fdc_read_id_tests)
endif()

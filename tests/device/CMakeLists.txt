if(BUILD_TESTING)
    add_executable(cartridge_tests
        cartridge_test.cpp
        ../../src/device/cartridge.c
    )

    target_include_directories(cartridge_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(cartridge_tests PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    )
    target_link_options(cartridge_tests PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<CXX_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    )
    target_link_libraries(cartridge_tests PRIVATE GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(cartridge_tests)
endif()

if(BUILD_TESTING)
    add_executable(ini_tests
        ini_test.cpp
        ../../src/utils/ini.c
    )

    target_include_directories(ini_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_link_libraries(ini_tests PRIVATE GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(ini_tests)
endif()

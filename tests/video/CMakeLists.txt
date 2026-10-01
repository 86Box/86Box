if(BUILD_TESTING)
    add_executable(wd90c31_tests
        wd90c31_test.cpp
        ../../src/video/vid_wd90c31.c
    )
    target_include_directories(wd90c31_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_link_libraries(wd90c31_tests PRIVATE GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(wd90c31_tests)
endif()

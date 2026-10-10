if(BUILD_TESTING)
    add_executable(hdd_image_tests
        hdd_image_test.cpp
        ../../src/disk/hdd_image.c
    )
    target_include_directories(hdd_image_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_link_libraries(hdd_image_tests PRIVATE GTest::gtest_main minivhd)

    include(GoogleTest)
    gtest_discover_tests(hdd_image_tests)
endif()

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

    add_executable(pc_block_tests
        pc_block_test.cpp
    )

    target_include_directories(pc_block_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
    )
    target_link_libraries(pc_block_tests PRIVATE GTest::gtest_main)

    add_executable(random_tests
        random_test.cpp
        ../../src/utils/random.c
    )

    target_include_directories(random_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
    )
    target_link_libraries(random_tests PRIVATE GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(ini_tests)
    gtest_discover_tests(pc_block_tests)
    gtest_discover_tests(random_tests)
endif()

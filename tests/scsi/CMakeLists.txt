if(BUILD_TESTING)
    add_executable(aic6360_tests aic6360_test.cpp ../../src/scsi/scsi_aic6360.c)
    target_include_directories(aic6360_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_link_libraries(aic6360_tests PRIVATE GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(aic6360_tests)
endif()

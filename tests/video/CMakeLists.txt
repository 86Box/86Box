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

    # The Mach64 tests include the sources they test.
    add_executable(mach64_accel_tests vid_ati_mach64_accel_test.c)
    target_include_directories(mach64_accel_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    add_test(NAME Mach64.Accel COMMAND mach64_accel_tests)

    add_executable(mach64_3d_tests vid_ati_mach64_3d_test.c)
    target_include_directories(mach64_3d_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    add_test(NAME Mach64.3D COMMAND mach64_3d_tests)
endif()

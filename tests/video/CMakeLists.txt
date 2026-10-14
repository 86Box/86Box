if(BUILD_TESTING)
    add_executable(s3_display_tests
        vid_s3_display_test.c
        ../../src/video/vid_svga_render.c
        ../../src/video/clockgen/vid_clockgen_ics2494.c
        ../../src/video/clockgen/vid_clockgen_icd2061.c
        ../../src/video/clockgen/vid_clockgen_av9194.c
    )
    target_include_directories(s3_display_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(s3_display_tests PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    )
    target_link_options(s3_display_tests PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    )
    add_test(NAME S3.Display COMMAND s3_display_tests)

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

    add_subdirectory(rage128)
endif()

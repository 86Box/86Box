if(BUILD_TESTING)
    add_executable(s3_display_tests
        vid_s3_display_test.c
        vid_s3_test_stubs.c
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

    add_executable(s3_accel_tests
        vid_s3_accel_test.c
        vid_s3_test_stubs.c
        ../../src/video/vid_svga_render.c
        ../../src/video/clockgen/vid_clockgen_ics2494.c
        ../../src/video/clockgen/vid_clockgen_icd2061.c
        ../../src/video/clockgen/vid_clockgen_av9194.c
    )
    target_include_directories(s3_accel_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(s3_accel_tests PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    )
    target_link_options(s3_accel_tests PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    )
    if(NOT MSVC)
        target_link_libraries(s3_accel_tests PRIVATE m)
    endif()
    add_test(NAME S3.Accel COMMAND s3_accel_tests)

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
    add_test(NAME Mach64.Palette COMMAND mach64_3d_tests --palette)
    add_test(NAME Mach64.VQ COMMAND mach64_3d_tests --vq)
    add_test(NAME Mach64.Mip COMMAND mach64_3d_tests --mip)
    add_test(NAME Mach64.Dither COMMAND mach64_3d_tests --dither)
    add_test(NAME Mach64.Composite COMMAND mach64_3d_tests --composite)
    add_test(NAME Mach64.Bilinear COMMAND mach64_3d_tests --bilinear)
    add_test(NAME Mach64.Modulate COMMAND mach64_3d_tests --modulate)

    add_executable(mach64_busmaster_tests vid_ati_mach64_busmaster_test.c)
    target_include_directories(mach64_busmaster_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    add_test(NAME Mach64.BusMaster COMMAND mach64_busmaster_tests)

    add_executable(mach64_idct_tests vid_ati_mach64_idct_test.c)
    target_include_directories(mach64_idct_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    add_test(NAME Mach64.IDCT COMMAND mach64_idct_tests)
    if(UNIX)
        target_link_libraries(mach64_idct_tests PRIVATE m)
    endif()

    foreach(target mach64_accel_tests mach64_3d_tests mach64_busmaster_tests)
        target_sources(${target} PRIVATE ../../src/video/vid_ati_mach64_idct.c)
        if(UNIX)
            target_link_libraries(${target} PRIVATE m)
        endif()
    endforeach()

    add_executable(mach64_display_tests vid_ati_mach64_display_test.c)
    target_include_directories(mach64_display_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    add_test(NAME Mach64.Display COMMAND mach64_display_tests)


    add_executable(ogc_deb_tests vid_ogc_deb_test.c)
    target_include_directories(ogc_deb_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    add_test(NAME OGC.DEB COMMAND ogc_deb_tests)

    # The Voodoo LOD test includes the renderer it tests.
    add_executable(voodoo_lod_log2_tests vid_voodoo_lod_log2_test.c)
    target_include_directories(voodoo_lod_log2_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    if(NOT MSVC AND (ARCH STREQUAL "i386" OR ARCH STREQUAL "x86_64"))
        target_compile_options(voodoo_lod_log2_tests PRIVATE "-msse2")
    endif()
    if(NOT MSVC)
        target_link_libraries(voodoo_lod_log2_tests PRIVATE m)
    endif()
    add_test(NAME Voodoo.LodLog2 COMMAND voodoo_lod_log2_tests)

    add_subdirectory(rage128)
endif()

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

    # The RIVA TNT 3D test includes the source it tests.
    add_executable(nv4_3d_swizzle_tests nv4_3d_swizzle_test.c)
    target_include_directories(nv4_3d_swizzle_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_link_libraries(nv4_3d_swizzle_tests PRIVATE m)
    add_test(NAME NV4.Swizzle3D COMMAND nv4_3d_swizzle_tests)

    add_executable(nv4_3d_ropfill_tests nv4_3d_ropfill_test.c)
    target_include_directories(nv4_3d_ropfill_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_link_libraries(nv4_3d_ropfill_tests PRIVATE m)
    add_test(NAME NV4.RopFill3D COMMAND nv4_3d_ropfill_tests)

    # The GPU lane against the CPU (skips itself without a Vulkan device)
    get_filename_component(NV4_TEST_CC_DIR ${CMAKE_C_COMPILER} DIRECTORY)
    find_path(NV4_TEST_VK_INCLUDE_DIR vulkan/vulkan.h HINTS ${NV4_TEST_CC_DIR}/../include NO_DEFAULT_PATH)
    if(NV4_TEST_VK_INCLUDE_DIR)
        add_executable(nv4_3d_gpu_tests nv4_3d_gpu_test.c
            ${PROJECT_SOURCE_DIR}/src/video/nv/nv4/subsystems/nv4_pgraph_3d_gpu.c
            ${PROJECT_SOURCE_DIR}/src/video/nv/nv_gpu_vk.c)
        target_compile_definitions(nv4_3d_gpu_tests PRIVATE NV4_GPU_HAVE_VULKAN)
        target_include_directories(nv4_3d_gpu_tests PRIVATE
            ${PROJECT_SOURCE_DIR}/src/include
            ${PROJECT_BINARY_DIR}/src/include
            ${NV4_TEST_VK_INCLUDE_DIR}
        )
        target_link_libraries(nv4_3d_gpu_tests PRIVATE m)
        add_test(NAME NV4.Gpu3D COMMAND nv4_3d_gpu_tests)

        # Recorded batches (NV4_3D_REPLAY_DIR) through the CPU and the lane
        add_executable(nv4_3d_replay_tests nv4_3d_replay_test.c
            ${PROJECT_SOURCE_DIR}/src/video/nv/nv4/subsystems/nv4_pgraph_3d_gpu.c
            ${PROJECT_SOURCE_DIR}/src/video/nv/nv_gpu_vk.c)
        target_compile_definitions(nv4_3d_replay_tests PRIVATE NV4_GPU_HAVE_VULKAN)
        target_include_directories(nv4_3d_replay_tests PRIVATE
            ${PROJECT_SOURCE_DIR}/src/include
            ${PROJECT_BINARY_DIR}/src/include
            ${NV4_TEST_VK_INCLUDE_DIR}
        )
        target_link_libraries(nv4_3d_replay_tests PRIVATE m)
        add_test(NAME NV4.Replay3D COMMAND nv4_3d_replay_tests)
    endif()

    add_subdirectory(rage128)
endif()

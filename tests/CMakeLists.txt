add_subdirectory(cdrom)
add_subdirectory(floppy)
add_subdirectory(device)

if(BUILD_TESTING)
    add_library(pcjx_floppy_engine OBJECT
        ../src/floppy/fdd_86f.c
        ../src/floppy/fdd_img.c
        ../src/utils/fifo.c
        ../src/utils/crc.c
    )
    target_include_directories(pcjx_floppy_engine PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(pcjx_floppy_engine PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    )
    target_compile_definitions(pcjx_floppy_engine PRIVATE
        tsc=pcjx_floppy_tsc TIMER_USEC=pcjx_floppy_TIMER_USEC
        timer_enable=pcjx_floppy_timer_enable timer_disable=pcjx_floppy_timer_disable
        timer_add=pcjx_floppy_timer_add random_generate=pcjx_floppy_random_generate
        fatal=pcjx_floppy_fatal pclog=pcjx_floppy_pclog pclog_ex=pcjx_floppy_pclog_ex
        plat_fopen=pcjx_floppy_plat_fopen path_get_extension=pcjx_floppy_path_get_extension
        floppy_ioctl_read_sector=pcjx_floppy_floppy_ioctl_read_sector
        floppy_ioctl_write_sector=pcjx_floppy_floppy_ioctl_write_sector
        floppy_ioctl_close=pcjx_floppy_floppy_ioctl_close
    )
    add_executable(pcjx_tests
        machine/pcjx_test.cpp
        floppy/pcjx_floppy_test.cpp
        ../src/machine/m_pcjx_scancodes.c
        ../src/video/video.c
        $<TARGET_OBJECTS:pcjx_floppy_engine>
    )
    target_include_directories(pcjx_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(pcjx_tests PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    )
    target_link_options(pcjx_tests PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<CXX_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    )
    target_link_libraries(pcjx_tests PRIVATE GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(pcjx_tests)
endif()

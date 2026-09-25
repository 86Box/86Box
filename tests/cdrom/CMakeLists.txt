if(BUILD_TESTING)
    add_executable(philips_cdrom_tests cdrom_philips_test.cpp ../../src/cdrom/cdrom_philips.c)
    target_include_directories(philips_cdrom_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/include ${PROJECT_BINARY_DIR}/src/include)
    target_link_libraries(philips_cdrom_tests PRIVATE GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(philips_cdrom_tests)

    add_executable(hitachi_cdrom_tests
        cdrom_hitachi_test.cpp
        ../../src/cdrom/cdrom_hitachi.c
    )
    target_include_directories(hitachi_cdrom_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_link_libraries(hitachi_cdrom_tests PRIVATE GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(hitachi_cdrom_tests)

    add_executable(mke_cdrom_tests
        cdrom_mke_test.cpp
        ../../src/cdrom/cdrom_mke.c
        ../../src/utils/fifo8.c
    )
    target_include_directories(mke_cdrom_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_link_libraries(mke_cdrom_tests PRIVATE GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(mke_cdrom_tests)

    add_executable(mitsumi_cdrom_tests
        cdrom_mitsumi_test.cpp
    )

    target_include_directories(mitsumi_cdrom_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(mitsumi_cdrom_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    )
    target_link_options(mitsumi_cdrom_tests PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<CXX_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    )
    target_link_libraries(mitsumi_cdrom_tests PRIVATE GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(mitsumi_cdrom_tests)
endif()

if(BUILD_BENCHMARKS)
    add_executable(mitsumi_cdrom_benchmarks
        cdrom_mitsumi_benchmark.cpp
    )
    target_include_directories(mitsumi_cdrom_benchmarks PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(mitsumi_cdrom_benchmarks PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    )
    target_link_options(mitsumi_cdrom_benchmarks PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<CXX_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    )
    target_link_libraries(mitsumi_cdrom_benchmarks PRIVATE benchmark::benchmark_main)
endif()

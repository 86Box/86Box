add_executable(mitsumi_cdrom_tests
    cdrom_mitsumi_test.cpp
)

target_include_directories(mitsumi_cdrom_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/include
    ${PROJECT_BINARY_DIR}/src/include
)
target_compile_options(mitsumi_cdrom_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-ffunction-sections;-fdata-sections>
)
target_link_options(mitsumi_cdrom_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wl,--gc-sections>
)
target_link_libraries(mitsumi_cdrom_tests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(mitsumi_cdrom_tests)

if(BUILD_TESTING)
    add_executable(cpu_tr_tests
        cpu_tr_test.cpp
        ../../src/cpu/cpu.c
    )
    add_executable(cpu_cache_tests
        cpu_cache_test.cpp
        vl82c486_cache_test.c
        ../../src/cpu/cpu.c
        ../../src/mem/mem.c
    )
    add_executable(x87_fistp_tests x87_fistp_test.cpp)
    add_executable(x87_fistp_new_tags_tests x87_fistp_test.cpp)
    target_compile_definitions(x87_fistp_new_tags_tests PRIVATE X87_TEST_NEW_TAGS)
    foreach(target cpu_tr_tests cpu_cache_tests x87_fistp_tests x87_fistp_new_tags_tests)
        target_include_directories(${target} PRIVATE
            ${PROJECT_SOURCE_DIR}/src/include
            ${PROJECT_SOURCE_DIR}/src/cpu
            ${PROJECT_BINARY_DIR}/src/include
        )
        target_compile_options(${target} PRIVATE
            $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
        )
        target_link_options(${target} PRIVATE
            $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
            $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<CXX_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
        )
        target_link_libraries(${target} PRIVATE GTest::gtest_main)
        include(GoogleTest)
        if(target MATCHES "^x87_fistp")
            target_compile_options(${target} PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-unused-function>
            )
            gtest_discover_tests(${target} TEST_PREFIX "${target}.")
        else()
            gtest_discover_tests(${target})
        endif()
    endforeach()
endif()

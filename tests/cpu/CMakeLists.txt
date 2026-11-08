if(BUILD_TESTING)
    add_executable(x87_f2xm1_tests x87_f2xm1_test.cpp)
    target_include_directories(x87_f2xm1_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/cpu)
    target_link_libraries(x87_f2xm1_tests PRIVATE softfloat3e)
    add_test(NAME x87_f2xm1_tests COMMAND x87_f2xm1_tests)

    add_executable(codegen_reuse_tests codegen_reuse_test.cpp)
    set(cpu_test_targets codegen_reuse_tests)
    foreach(target ${cpu_test_targets})
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
        gtest_discover_tests(${target})
    endforeach()
endif()

if(BUILD_TESTING)
    add_executable(cpu_tr_tests
        cpu_tr_test.cpp
        ../../src/cpu/cpu.c
    )
    add_executable(cpu_interrupt_tests
        x86_interrupt_test.cpp
        ../../src/pic.c
    )
    add_executable(codegen_reuse_tests codegen_reuse_test.cpp)
    if(DYNAREC)
        add_executable(codegen_shift_cl_tests
            codegen_shift_cl_test.cpp
            ../../src/codegen/codegen_ops_shift.c
        )
        target_include_directories(codegen_shift_cl_tests PRIVATE
            ${PROJECT_SOURCE_DIR}/src/codegen
        )
        target_compile_definitions(codegen_shift_cl_tests PRIVATE USE_DYNAREC)
    endif()
    add_executable(cpu_cache_tests
        cpu_cache_test.cpp
        vl82c486_cache_test.c
        ../../src/cpu/cpu.c
        ../../src/mem/mem.c
    )
    add_executable(x87_fistp_tests x87_fistp_test.cpp ../../src/cpu/x87_ferr.c)
    add_executable(x87_fistp_new_tags_tests x87_fistp_test.cpp ../../src/cpu/x87_ferr.c)
    set(cpu_test_targets cpu_tr_tests cpu_interrupt_tests codegen_reuse_tests cpu_cache_tests x87_fistp_tests x87_fistp_new_tags_tests)
    if(TARGET codegen_shift_cl_tests)
        list(APPEND cpu_test_targets codegen_shift_cl_tests)
    endif()
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

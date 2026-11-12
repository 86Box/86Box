if(BUILD_TESTING)
    add_executable(x87_f2xm1_tests x87_f2xm1_test.cpp)
    target_include_directories(x87_f2xm1_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/cpu)
    target_link_libraries(x87_f2xm1_tests PRIVATE softfloat3e)
    add_test(NAME x87_f2xm1_tests COMMAND x87_f2xm1_tests)

    set(GDBSTUB_TEST_PORT "" CACHE STRING "Port of a disposable paused 386+ guest for GDB integration tests")
    set(GDBSTUB_TEST_BINARY "" CACHE FILEPATH "86Box executable for isolated GDB integration tests")
    set(GDBSTUB_TEST_ROMPATH "" CACHE PATH "Installed ROM directory for isolated GDB integration tests")
    if(GDBSTUB_TEST_PORT OR GDBSTUB_TEST_BINARY)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
    endif()
    if(GDBSTUB_TEST_PORT)
        add_test(NAME gdbstub_guest
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/gdbstub_test.py --port ${GDBSTUB_TEST_PORT})
        set_tests_properties(gdbstub_guest PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
    endif()
    if(GDBSTUB_TEST_BINARY)
        if(NOT GDBSTUB_TEST_ROMPATH)
            message(FATAL_ERROR "GDBSTUB_TEST_BINARY requires GDBSTUB_TEST_ROMPATH")
        endif()
        add_test(NAME gdbstub_isolated_guest
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/gdbstub_test.py
                --binary ${GDBSTUB_TEST_BINARY} --rompath ${GDBSTUB_TEST_ROMPATH} --runtime)
        set_tests_properties(gdbstub_isolated_guest PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
        if(DYNAREC)
            add_test(NAME gdbstub_isolated_dynarec_off
                COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/gdbstub_test.py
                    --binary ${GDBSTUB_TEST_BINARY} --rompath ${GDBSTUB_TEST_ROMPATH}
                    --runtime --guest-debug-regs off)
            set_tests_properties(gdbstub_isolated_dynarec_off
                PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
        endif()
        add_test(NAME gdbstub_isolated_fxsr
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/gdbstub_test.py
                --binary ${GDBSTUB_TEST_BINARY} --rompath ${GDBSTUB_TEST_ROMPATH}
                --runtime --cpu pentium2-xeon GuestTests.test_fx_image_checksum_watchpoints)
        set_tests_properties(gdbstub_isolated_fxsr PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
        foreach(debug_regs on off)
            add_test(NAME gdbstub_isolated_interpreter_${debug_regs}
                COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/gdbstub_test.py
                    --binary ${GDBSTUB_TEST_BINARY} --rompath ${GDBSTUB_TEST_ROMPATH}
                    --runtime --interpreter --guest-debug-regs ${debug_regs})
            set_tests_properties(gdbstub_isolated_interpreter_${debug_regs}
                PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
        endforeach()
        if(AUTOMATION AND QT AND UNIX)
            add_test(NAME gdbstub_runtime
                COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/gdbstub_runtime_test.py
                    --binary ${GDBSTUB_TEST_BINARY} --rompath ${GDBSTUB_TEST_ROMPATH})
            set_tests_properties(gdbstub_runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
        endif()
    endif()
    add_executable(codegen_reuse_tests codegen_reuse_test.cpp)
    if(DYNAREC)
        if(UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
            add_executable(codegen_rx_tests codegen_rx_test.cpp codegen_rx_fixture.c)
            target_include_directories(codegen_rx_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/codegen)
            target_compile_definitions(codegen_rx_tests PRIVATE USE_DYNAREC)
        endif()
    endif()
    add_executable(x87_fprem_tests x87_fprem_test.cpp)
    add_executable(gdbstub_watch_tests gdbstub_watch_test.cpp ../../src/mem/mem.c)
    add_executable(mem_flush_write_tests mem_flush_write_test.cpp ../../src/mem/mem.c)
    set(cpu_test_targets codegen_reuse_tests x87_fprem_tests gdbstub_watch_tests mem_flush_write_tests)
    if(TARGET codegen_rx_tests)
        list(APPEND cpu_test_targets codegen_rx_tests)
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
        gtest_discover_tests(${target})
    endforeach()
endif()

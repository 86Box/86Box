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
        if(AUTOMATION AND QT AND UNIX)
            add_test(NAME gdbstub_runtime
                COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/gdbstub_runtime_test.py
                    --binary ${GDBSTUB_TEST_BINARY} --rompath ${GDBSTUB_TEST_ROMPATH})
            set_tests_properties(gdbstub_runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
        endif()
    endif()
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
    add_executable(gdbstub_watch_tests gdbstub_watch_test.cpp)
    set(cpu_test_targets codegen_reuse_tests gdbstub_watch_tests)
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
        gtest_discover_tests(${target})
    endforeach()
endif()

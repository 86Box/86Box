if(BUILD_TESTING)
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
    add_executable(x87_fistp_tests x87_fistp_test.cpp ../../src/cpu/x87_ferr.c)
    add_executable(gdbstub_watch_tests gdbstub_watch_test.cpp)
    add_executable(x87_fistp_new_tags_tests x87_fistp_test.cpp ../../src/cpu/x87_ferr.c)
    foreach(target cpu_tr_tests cpu_cache_tests x87_fistp_tests x87_fistp_new_tags_tests gdbstub_watch_tests)
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

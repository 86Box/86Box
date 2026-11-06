if(BUILD_TESTING)
    add_executable(rtl8139_timer_tests rtl8139_timer_test.c ../../src/timer.c)
    target_include_directories(rtl8139_timer_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(rtl8139_timer_tests PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    )
    target_link_options(rtl8139_timer_tests PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    )
    add_test(NAME rtl8139_timer COMMAND rtl8139_timer_tests)

    add_executable(rtl8139_rx_tests rtl8139_rx_test.c)
    target_include_directories(rtl8139_rx_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(rtl8139_rx_tests PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    )
    target_link_options(rtl8139_rx_tests PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    )
    add_test(NAME rtl8139_rx COMMAND rtl8139_rx_tests)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NETWORK_TEST_SLIRP REQUIRED IMPORTED_TARGET slirp)
    add_executable(network_queue_tests network_queue_test.c)
    target_include_directories(network_queue_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(network_queue_tests PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    )
    target_link_options(network_queue_tests PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    )
    target_link_libraries(network_queue_tests PRIVATE PkgConfig::NETWORK_TEST_SLIRP)
    add_test(NAME network_queue COMMAND network_queue_tests)
endif()

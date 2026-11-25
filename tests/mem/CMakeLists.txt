if(BUILD_TESTING)
    add_executable(row_boundary_test row_boundary_test.c)
    target_include_directories(row_boundary_test PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(row_boundary_test PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    )
    target_link_options(row_boundary_test PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    )
    add_test(NAME row_boundary_test COMMAND row_boundary_test)

    find_package(Threads REQUIRED)
    add_executable(phys_read_thread_test phys_read_thread_test.c)
    target_include_directories(phys_read_thread_test PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(phys_read_thread_test PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
        # The test includes mem.c, where Clang warns that the inline
        # mem_phys_written() calls a static function; the warning is about
        # mem.c's layout, not about the test.
        $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-static-in-inline>
    )
    target_link_options(phys_read_thread_test PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    )
    target_link_libraries(phys_read_thread_test PRIVATE Threads::Threads)
    add_test(NAME phys_read_thread_test COMMAND phys_read_thread_test)

    add_executable(phys_write_thread_test phys_write_thread_test.c)
    target_include_directories(phys_write_thread_test PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_compile_options(phys_write_thread_test PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
        # As for phys_read_thread_test: the warning is about mem.c's layout.
        $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-static-in-inline>
    )
    target_link_options(phys_write_thread_test PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    )
    target_link_libraries(phys_write_thread_test PRIVATE Threads::Threads)
    add_test(NAME phys_write_thread_test COMMAND phys_write_thread_test)
endif()

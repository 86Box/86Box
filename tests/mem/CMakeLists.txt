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
endif()

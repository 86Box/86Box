find_program(MEDIA_CARGO cargo REQUIRED)
set(archive_target_dir "${CMAKE_CURRENT_BINARY_DIR}/cargo")
set(archive_library "${archive_target_dir}/release/${CMAKE_STATIC_LIBRARY_PREFIX}media_archive${CMAKE_STATIC_LIBRARY_SUFFIX}")
add_custom_command(OUTPUT "${archive_library}"
    COMMAND "${MEDIA_CARGO}" build --locked --release --target-dir "${archive_target_dir}"
            --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
    DEPENDS Cargo.toml Cargo.lock src/lib.rs
    VERBATIM
)
add_custom_target(media_archive_build DEPENDS "${archive_library}")
add_library(media_archive STATIC IMPORTED GLOBAL)
set_target_properties(media_archive PROPERTIES IMPORTED_LOCATION "${archive_library}")
add_dependencies(media_archive media_archive_build)
if(WIN32)
    target_link_libraries(media_archive INTERFACE ws2_32 userenv bcrypt ntdll dbghelp)
elseif(UNIX)
    target_link_libraries(media_archive INTERFACE ${CMAKE_DL_LIBS} pthread m)
endif()

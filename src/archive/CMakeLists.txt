if(VCPKG_TOOLCHAIN)
    find_package(minizip CONFIG REQUIRED)
    if(TARGET MINIZIP::minizip)
        set(minizip_target MINIZIP::minizip)
    elseif(TARGET MINIZIP::minizipstatic)
        set(minizip_target MINIZIP::minizipstatic)
    else()
        message(FATAL_ERROR "The minizip package did not provide a library target")
    endif()
else()
    find_package(PkgConfig REQUIRED)
    if(minizip_ROOT)
        list(APPEND CMAKE_PREFIX_PATH "${minizip_ROOT}")
    endif()
    pkg_check_modules(MINIZIP REQUIRED IMPORTED_TARGET minizip)
    set(minizip_target PkgConfig::MINIZIP)
endif()

set(lzma_sdk_dir "${CMAKE_CURRENT_SOURCE_DIR}/vendor/lzma-sdk")

set(lzma_sdk_sources
    7zArcIn 7zBuf 7zBuf2 7zCrc 7zCrcOpt 7zDec 7zStream Bcj2 Bra Bra86 BraIA64
    CpuArch Delta LzmaDec Lzma2Dec Ppmd7 Ppmd7Dec
)
list(TRANSFORM lzma_sdk_sources PREPEND "${lzma_sdk_dir}/")
list(TRANSFORM lzma_sdk_sources APPEND ".c")
add_library(media_7z_decoder STATIC ${lzma_sdk_sources})
target_include_directories(media_7z_decoder SYSTEM PUBLIC "${lzma_sdk_dir}")
target_compile_definitions(media_7z_decoder PRIVATE Z7_PPMD_SUPPORT Z7_EXTRACT_ONLY)

add_library(media_archive STATIC media_archive.cpp)
target_include_directories(media_archive PRIVATE "${PROJECT_SOURCE_DIR}/src/include")
target_link_libraries(media_archive PRIVATE ${minizip_target} media_7z_decoder)

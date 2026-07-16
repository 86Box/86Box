cmake_minimum_required(VERSION 3.10)

project(chdr VERSION 0.3.0 LANGUAGES C)

if(CMAKE_PROJECT_NAME STREQUAL "chdr")
  option(BUILD_SHARED_LIBS "Build libchdr also as a shared library" ON)
endif()
option(INSTALL_STATIC_LIBS "Install static libraries" OFF)
option(WITH_SYSTEM_ZLIB "Use system provided zlib library" OFF)
option(WITH_SYSTEM_ZSTD "Use system provided zstd library" OFF)
option(CHDR_WANT_RAW_DATA_SECTOR "Output ECC data and sync header" ON)
option(CHDR_WANT_SUBCODE "Output CD subchannel data" ON)
option(CHDR_VERIFY_BLOCK_CRC "Verify integrity of decoded data" ON)

option(BUILD_LTO "Compile libchdr with link-time optimization if supported" OFF)
if(BUILD_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT HAVE_IPO)
  if(HAVE_IPO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

option(BUILD_FUZZER "Build instrumented binary for fuzzing with libfuzzer, requires clang")
if(BUILD_FUZZER)
  # Override CFLAGS early for instrumentation. Disable shared libs for instrumentation.
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,fuzzer-no-link")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,fuzzer-no-link")
  set(BUILD_SHARED_LIBS OFF)
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)

#--------------------------------------------------
# dependencies
#--------------------------------------------------

# lzma
if(NOT TARGET chdr-lzma)
  add_subdirectory(deps/lzma-25.01 EXCLUDE_FROM_ALL)
endif()
list(APPEND CHDR_LIBS chdr-lzma)

# zlib
if (WITH_SYSTEM_ZLIB)
  find_package(ZLIB REQUIRED)
  list(APPEND PLATFORM_LIBS ZLIB::ZLIB)
  list(APPEND CHDR_DEFINES CHDR_SYSTEM_ZLIB)
else()
  if(NOT TARGET miniz)
    add_subdirectory(deps/miniz-3.1.1 EXCLUDE_FROM_ALL)
  endif()
  list(APPEND CHDR_LIBS miniz)
endif()

# zstd
if (WITH_SYSTEM_ZSTD)
  find_package(zstd MODULE REQUIRED)
  list(APPEND PLATFORM_LIBS zstd::zstd)
  list(APPEND CHDR_DEFINES CHDR_SYSTEM_ZSTD)
else()
  if(NOT TARGET zstd)
    add_subdirectory(deps/zstd-1.5.7 EXCLUDE_FROM_ALL)
  endif()
  list(APPEND CHDR_LIBS zstd)
endif()

#--------------------------------------------------
# options
#--------------------------------------------------

if(CHDR_WANT_RAW_DATA_SECTOR)
  list(APPEND CHDR_DEFINES WANT_RAW_DATA_SECTOR=1)
else()
  list(APPEND CHDR_DEFINES WANT_RAW_DATA_SECTOR=0)
endif()

if(CHDR_WANT_SUBCODE)
  list(APPEND CHDR_DEFINES WANT_SUBCODE=1)
else()
  list(APPEND CHDR_DEFINES WANT_SUBCODE=0)
endif()

if(CHDR_VERIFY_BLOCK_CRC)
  list(APPEND CHDR_DEFINES VERIFY_BLOCK_CRC=1)
else()
  list(APPEND CHDR_DEFINES VERIFY_BLOCK_CRC=0)
endif()

#--------------------------------------------------
# chdr
#--------------------------------------------------

set(CHDR_SOURCES
  src/libchdr_bitstream.c
  src/libchdr_cdrom.c
  src/libchdr_chd.c
  src/libchdr_codec_cdfl.c
  src/libchdr_codec_cdlz.c
  src/libchdr_codec_cdzl.c
  src/libchdr_codec_cdzs.c
  src/libchdr_codec_flac.c
  src/libchdr_codec_huff.c
  src/libchdr_codec_lzma.c
  src/libchdr_codec_zlib.c
  src/libchdr_codec_zstd.c
  src/libchdr_flac.c
  src/libchdr_huffman.c
)

add_library(chdr-static STATIC ${CHDR_SOURCES})
target_include_directories(chdr-static INTERFACE include)
target_link_libraries(chdr-static PRIVATE ${CHDR_LIBS} ${PLATFORM_LIBS})
target_compile_definitions(chdr-static PRIVATE ${CHDR_DEFINES})

if(MSVC)
  target_compile_definitions(chdr-static PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if (INSTALL_STATIC_LIBS)
  install(TARGETS chdr-static ${CHDR_LIBS}
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  )
endif()

if (BUILD_SHARED_LIBS)
  add_library(chdr SHARED ${CHDR_SOURCES})
  target_include_directories(chdr INTERFACE include)
  target_link_libraries(chdr PRIVATE ${CHDR_LIBS} ${PLATFORM_LIBS})
  target_compile_definitions(chdr PRIVATE ${CHDR_DEFINES})

  if(MSVC)
    target_compile_definitions(chdr PUBLIC "CHD_DLL")
    target_compile_definitions(chdr PRIVATE "CHD_DLL_EXPORTS")
    target_compile_definitions(chdr PRIVATE _CRT_SECURE_NO_WARNINGS)
  elseif(APPLE)
    target_link_libraries(chdr PRIVATE -Wl,-dead_strip -Wl,-exported_symbol,_chd_*)
  else()
    include(CheckLinkerFlag)
    check_linker_flag(C "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/link.T" LINKER_VERSION_SCRIPT_SUPPORTED)
    if(LINKER_VERSION_SCRIPT_SUPPORTED)
      target_link_options(chdr PRIVATE "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/link.T")
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL OpenBSD)
      target_link_libraries(chdr PRIVATE -Wl,--no-undefined)
    endif()
  endif()

  set_target_properties(chdr PROPERTIES C_VISIBILITY_PRESET hidden)
  set_target_properties(chdr PROPERTIES VISIBILITY_INLINES_HIDDEN 1)
  set_target_properties(chdr PROPERTIES PUBLIC_HEADER "include/libchdr/bitstream.h;include/libchdr/cdrom.h;include/libchdr/chd.h;include/libchdr/chdconfig.h;include/libchdr/coretypes.h;include/libchdr/flac.h;include/libchdr/huffman.h;include/libchdr/macros.h")
  set_target_properties(chdr PROPERTIES VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}" SOVERSION ${PROJECT_VERSION_MAJOR})

  if (CMAKE_BUILD_TYPE MATCHES Release)
    #add_custom_command(TARGET chdr POST_BUILD COMMAND ${CMAKE_STRIP} libchdr.so)
  endif (CMAKE_BUILD_TYPE MATCHES Release)

  install(TARGETS chdr
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/libchdr"
  )

  configure_file(pkg-config.pc.in ${CMAKE_BINARY_DIR}/libchdr.pc @ONLY)
  install(FILES ${CMAKE_BINARY_DIR}/libchdr.pc DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endif()

add_subdirectory(tests)

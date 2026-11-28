cmake_minimum_required(VERSION 3.20)
project(sid2pmd_trace LANGUAGES CXX)
include(FetchContent)
# Fixed official release, including assembled C64 player data.
FetchContent_Declare(sidcore URL
  https://github.com/libsidplayfp/libsidplayfp/releases/download/v3.1.1/libsidplayfp-3.1.1.tar.gz
  URL_HASH SHA256=12b79190593bf480b2d11481b5c2de62bac07f344437a66cd8d887329875c626
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sidcore)
set(core ${sidcore_SOURCE_DIR}/src)
file(READ ${sidcore_SOURCE_DIR}/Makefile.am makefile)
string(REGEX MATCH "src_libsidplayfp_la_SOURCES =([^=]+)src_libsidplayfp_la_LDFLAGS" source_block "${makefile}")
string(REGEX MATCHALL "src/[A-Za-z0-9_/.-]+\\.cpp" sources "${source_block}")
list(TRANSFORM sources PREPEND "${sidcore_SOURCE_DIR}/")
file(GLOB lite_sources ${core}/builders/sidlite-builder/sidlite/*.cpp)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/sidplayfp)
set(HAVE_BUILTIN_EXPECT 0)
configure_file(${core}/builders/sidlite-builder/sidlite/sl_defs.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/sl_defs.h @ONLY)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/include/sidplayfp/sidversion.h
  "#define LIBSIDPLAYFP_VERSION_MAJ 3\n#define LIBSIDPLAYFP_VERSION_MIN 1\n#define LIBSIDPLAYFP_VERSION_LEV 1\n")
add_executable(sidtrace sidtrace.cpp ${sources} ${lite_sources})
target_compile_features(sidtrace PRIVATE cxx_std_17)
target_compile_definitions(sidtrace PRIVATE HAVE_CXX17=1 PACKAGE_NAME="libsidplayfp" PACKAGE_URL="https://github.com/libsidplayfp/libsidplayfp" PACKAGE_VERSION="3.1.1" VERSION="3.1.1")
target_include_directories(sidtrace PRIVATE ${core} ${CMAKE_CURRENT_BINARY_DIR}/include)
if(MINGW)
  target_link_options(sidtrace PRIVATE -static -static-libgcc -static-libstdc++)
endif()

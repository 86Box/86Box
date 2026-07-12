#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#
# Authors: David Hrdlička, <hrdlickadavid@outlook.com>
#          Jasmine Iwanek, <jriwanek@gmail.com>
#
#          Copyright 2020-2021 David Hrdlička.
#          Copyright 2024-2026 Jasmine Iwanek.
#

set(AARU_BUILD_PACKAGE ON)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(libaaruformat)
find_package(PkgConfig REQUIRED)

pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)

# AARU
add_library(cdrom_aaru OBJECT
    cdrom_aaru.c)

if(ARCH STREQUAL "arm64")
if(APPLE)
    target_compile_options(cdrom_aaru PRIVATE -Wno-unknown-pragmas -Wno-strict-prototypes -Wno-parentheses -Wno-unused-variable -Wno-old-style-definition -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-variable -Wno-int-conversion -Wno-typedef-redefinition -Wno-inline -Wno-incompatible-pointer-types -Wno-pointer-sign -w)

    target_compile_options(aaruformat PRIVATE -DBLAKE3_USE_NEON=0 -Wno-unknown-pragmas -Wno-strict-prototypes -Wno-parentheses -Wno-unused-variable -Wno-old-style-definition -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-variable -Wno-int-conversion -Wno-typedef-redefinition -Wno-inline -Wno-incompatible-pointer-types -Wno-pointer-sign -w)
else()
    target_compile_options(cdrom_aaru PRIVATE -Wno-unknown-pragmas -Wno-strict-prototypes -Wno-parentheses -Wno-unused-variable -Wno-old-style-definition -Wno-dangling-pointer -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-variable -Wno-int-conversion -Wno-typedef-redefinition -Wno-inline -Wno-incompatible-pointer-types -Wno-pointer-sign -w)

    target_compile_options(aaruformat PRIVATE -DBLAKE3_USE_NEON=0 -Wno-unknown-pragmas -Wno-strict-prototypes -Wno-parentheses -Wno-unused-variable -Wno-old-style-definition -Wno-dangling-pointer -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-variable -Wno-int-conversion -Wno-typedef-redefinition -Wno-inline -Wno-incompatible-pointer-types -Wno-pointer-sign -w)
endif()
else()
if(APPLE)
    target_compile_options(cdrom_aaru PRIVATE -Wno-unknown-pragmas -Wno-strict-prototypes -Wno-parentheses -Wno-unused-variable -Wno-old-style-definition -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-variable -Wno-int-conversion -Wno-typedef-redefinition -Wno-inline -Wno-incompatible-pointer-types -Wno-pointer-sign -w)

    target_compile_options(aaruformat PRIVATE -Wno-unknown-pragmas -Wno-strict-prototypes -Wno-parentheses -Wno-unused-variable -Wno-old-style-definition -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-variable -Wno-int-conversion -Wno-typedef-redefinition -Wno-inline -Wno-incompatible-pointer-types -Wno-pointer-sign -w)
else()
    target_compile_options(cdrom_aaru PRIVATE -Wno-unknown-pragmas -Wno-strict-prototypes -Wno-parentheses -Wno-unused-variable -Wno-old-style-definition -Wno-dangling-pointer -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-variable -Wno-int-conversion -Wno-typedef-redefinition -Wno-inline -Wno-incompatible-pointer-types -Wno-pointer-sign -w)

    target_compile_options(aaruformat PRIVATE -Wno-unknown-pragmas -Wno-strict-prototypes -Wno-parentheses -Wno-unused-variable -Wno-old-style-definition -Wno-dangling-pointer -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-variable -Wno-int-conversion -Wno-typedef-redefinition -Wno-inline -Wno-incompatible-pointer-types -Wno-pointer-sign -w)
endif()
endif()

target_compile_options(cdrom_aaru PRIVATE "$<$<CONFIG:DEBUG>:-O3>")

string(REPLACE "-Og" "-O3" CMAKE_C_FLAGS_DEBUG ${CMAKE_C_FLAGS_DEBUG})

target_include_directories(cdrom_aaru PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libaaruformat/include/")

if(APPLE OR WIN32)
target_link_libraries(cdrom_aaru -static aaruformat)
else()
target_link_libraries(cdrom_aaru aaruformat)
endif()

add_library(cdrom OBJECT
    cdrom.c
    cdrom_image.c
    cdrom_image_viso.c
    cdrom_mke.c
)
target_link_libraries(86Box cdrom_aaru)
string(REPLACE "-O3" "-Og" CMAKE_C_FLAGS_DEBUG ${CMAKE_C_FLAGS_DEBUG})
if(NOT WIN32)
	target_include_directories(86Box PRIVATE PkgConfig::SNDFILE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
	target_include_directories(cdrom PRIVATE /usr/local/include)
endif()
target_link_libraries(86Box PkgConfig::SNDFILE)

if(CDROM_MITSUMI)
    target_compile_definitions(cdrom PRIVATE USE_CDROM_MITSUMI)
    target_sources(cdrom PRIVATE cdrom_mitsumi.c)
endif()

if (WIN32)
    # MSYS2
    target_link_libraries(86Box -static ${SNDFILE_STATIC_LIBRARIES})
endif()

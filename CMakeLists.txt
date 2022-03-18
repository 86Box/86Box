#
# 86Box     A hypervisor and IBM PC system emulator that specializes in
#           running old operating systems and software designed for IBM
#           PC systems and compatibles from 1981 through fairly recent
#           system designs based on the PCI bus.
#
#           This file is part of the 86Box distribution.
#
#           CMake build script.
#
# Authors:  David Hrdlička, <hrdlickadavid@outlook.com>
#
#           Copyright 2020,2021 David Hrdlička.
#

cmake_minimum_required(VERSION 3.16)

cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0079 NEW)

if(NOT DEFINED QT OR QT)
    list(APPEND VCPKG_MANIFEST_FEATURES "qt-ui")
endif()

if(OPENAL)
    list(APPEND VCPKG_MANIFEST_FEATURES "openal")
endif()

if(SLIRP_EXTERNAL)
    list(APPEND VCPKG_MANIFEST_FEATURES "slirp")
endif()

if(MUNT_EXTERNAL)
    list(APPEND VCPKG_MANIFEST_FEATURES "munt")
endif()

project(86Box
    VERSION 3.3
    DESCRIPTION "Emulator of x86-based systems"
    HOMEPAGE_URL "https://86box.net"
    LANGUAGES C CXX)

include(CPack)
include(CMakeDependentOption)

# Basic build options
if(VCPKG_TOOLCHAIN)
    # For vcpkg builds we have to respect the linking method used by the
    # specified triplet.
    set(NO_STATIC_OPTION ON)
    if(VCPKG_TARGET_TRIPLET MATCHES "-static$")
        # `-static` triplet, use static linking
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set(STATIC_BUILD ON)
    elseif(VCPKG_TARGET_TRIPLET MATCHES "-static-md$")
        # `-static-md` triplet, use static linking with dynamic CRT
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        set(STATIC_BUILD ON)
    elseif()
        # Regular triplet, use dynamic linking
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        set(STATIC_BUILD OFF)
    endif()
endif()

if(WIN32)
    # Prefer static builds on Windows
    set(PREFER_STATIC ON)

    # Default value for the `WIN32` target property, which specifies whether
    # to build the application for the Windows GUI or console subsystem
    option(CMAKE_WIN32_EXECUTABLE "Build a Windows GUI executable" ON)
else()
    # Prefer dynamic builds everywhere else
    set(PREFER_STATIC OFF)
endif()

if(APPLE)
    option(CMAKE_MACOSX_BUNDLE "Build a macOS bundle (.app)" ON)
endif()

if(NOT NO_STATIC_OPTION)
    if(PREFER_STATIC)
        option(STATIC_BUILD "Static build" ON)
    else()
        option(STATIC_BUILD "Static build" OFF)
    endif()
endif()

# Detect the target architecture by trying to compile `src/arch_detect.c`
try_compile(RESULT_VAR ${CMAKE_BINARY_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/src/arch_detect.c" OUTPUT_VARIABLE ARCH)
string(REGEX MATCH "ARCH ([a-zA-Z0-9_]+)" ARCH "${ARCH}")
string(REPLACE "ARCH " "" ARCH "${ARCH}")
if (NOT ARCH)
    set(ARCH unknown)
endif()

add_compile_definitions(CMAKE)
add_compile_definitions("$<$<CONFIG:Debug>:DEBUG>")

if(WIN32)
    # Disables *_s function warnings
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

    # Disables POSIX name warnings
    add_compile_definitions(_CRT_NONSTDC_NO_WARNINGS)

    # Disables WinSock deprecation warnings
    add_compile_definitions(_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

if(HAIKU)
    set(OPENAL ON)
    set(RTMIDI OFF)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

# Optional features
#
#      Option       Description                                                     Def.
#      ------       -----------                                                     ----
option(RELEASE      "Release build"                                                 OFF)
option(DYNAREC      "Dynamic recompiler"                                            ON)
option(OPENAL       "OpenAL"                                                        OFF)
option(RTMIDI       "RtMidi"                                                        ON)
option(FLUIDSYNTH   "FluidSynth"                                                    ON)
option(MUNT         "MUNT"                                                          ON)
option(DINPUT       "DirectInput"                                                   OFF)
option(CPPTHREADS   "C++11 threads"                                                 ON)
option(NEW_DYNAREC  "Use the PCem v15 (\"new\") dynamic recompiler"                 OFF)
option(MINITRACE    "Enable Chrome tracing using the modified minitrace library"    OFF)
option(GDBSTUB      "Enable GDB stub server for debugging"                          OFF)
option(DEV_BRANCH   "Development branch"                                            OFF)
option(QT           "Qt GUI"                                                        ON)

# Development branch features
#
#                      Option         Description                                   Def.    Condition       Otherwise
#                      ------         -----------                                   ----    ---------       ---------
cmake_dependent_option(AMD_K5         "AMD K5"                                      ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(CYRIX_6X86     "Cyrix 6x86"                                  ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(GUSMAX         "Gravis UltraSound MAX"                       ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(ISAMEM_RAMPAGE "AST Rampage"                                 ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(ISAMEM_IAB     "Intel Above Board"                           ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(ISAMEM_BRAT    "BocaRAM/AT"                                  ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(LASERXT        "VTech Laser XT"                              ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(MGA            "Matrox Mystique graphics adapters"           ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(NO_SIO         "Machines without emulated Super I/O chips"   ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(OLIVETTI       "Olivetti M290"                               ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(OPEN_AT        "OpenAT"                                      ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(PAS16          "Pro Audio Spectrum 16"                       ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(SIO_DETECT     "Super I/O Detection Helper"                  ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(TANDY_ISA      "Tandy PSG ISA clone boards"                  ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(VGAWONDER      "ATI VGA Wonder (ATI-18800)"                  ON      "DEV_BRANCH"    OFF)
cmake_dependent_option(VNC            "VNC renderer"                                OFF     "DEV_BRANCH"    OFF)
cmake_dependent_option(XL24           "ATI VGA Wonder XL24 (ATI-28800-6)"           ON      "DEV_BRANCH"    OFF)

# Ditto but for Qt
if(QT)
    option(USE_QT6 "Use Qt6 instead of Qt5" OFF)
endif()

# Determine the build type
set(RELEASE_BUILD   OFF)
set(BETA_BUILD      OFF)
set(ALPHA_BUILD     OFF)

string(TOLOWER "${BUILD_TYPE}" BUILD_TYPE_LOWER)
if(BUILD_TYPE_LOWER STREQUAL "release")
    # Release build
    set(RELEASE_BUILD ON)
    add_compile_definitions(RELEASE_BUILD)
elseif(BUILD_TYPE_LOWER STREQUAL "beta")
    # Beta build
    set(BETA_BUILD ON)
    add_compile_definitions(BETA_BUILD)
elseif(BUILD_TYPE_LOWER STREQUAL "alpha")
    # Alpha build
    set(ALPHA_BUILD ON)
    add_compile_definitions(ALPHA_BUILD)
endif()

# Variables introduced by richardg867 for versioning stuff
if(NOT CMAKE_PROJECT_VERSION_PATCH)
    set(CMAKE_PROJECT_VERSION_PATCH 0)
endif()
if(NOT EMU_BUILD_NUM)
    set(EMU_BUILD_NUM 0)
endif()
if(NOT EMU_COPYRIGHT_YEAR)
    set(EMU_COPYRIGHT_YEAR 2022)
endif()

add_subdirectory(src)

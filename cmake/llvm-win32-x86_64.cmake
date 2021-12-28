#
# 86Box     A hypervisor and IBM PC system emulator that specializes in
#           running old operating systems and software designed for IBM
#           PC systems and compatibles from 1981 through fairly recent
#           system designs based on the PCI bus.
#
#           This file is part of the 86Box distribution.
#
#           CMake toolchain file for Clang on Windows builds (x64/AMD64 target).
#
# Authors:  David Hrdlička, <hrdlickadavid@outlook.com>
#
#           Copyright 2021 David Hrdlička.
#

include(${CMAKE_CURRENT_LIST_DIR}/flags-gcc-x86_64.cmake)

# Use the GCC-compatible Clang executables in order to use our flags
set(CMAKE_C_COMPILER    clang)
set(CMAKE_CXX_COMPILER  clang++)

# `llvm-rc` is barely usable as of LLVM 13, using MS' rc.exe for now
set(CMAKE_RC_COMPILER   rc)

set(CMAKE_C_COMPILER_TARGET     x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET   x86_64-pc-windows-msvc)

set(CMAKE_SYSTEM_PROCESSOR AMD64)

# TODO: set the vcpkg target triplet perhaps?
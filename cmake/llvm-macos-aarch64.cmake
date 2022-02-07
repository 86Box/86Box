#
# 86Box     A hypervisor and IBM PC system emulator that specializes in
#           running old operating systems and software designed for IBM
#           PC systems and compatibles from 1981 through fairly recent
#           system designs based on the PCI bus.
#
#           This file is part of the 86Box distribution.
#
#           CMake toolchain file defining Clang compiler flags
#           for AArch64 (ARM64)-based Apple Silicon targets.
#
# Authors:  David Hrdlička, <hrdlickadavid@outlook.com>
#           dob205
#
#           Copyright 2021 David Hrdlička.
#           Copyright 2022 dob205.
#

string(APPEND CMAKE_C_FLAGS_INIT    " -march=armv8.5-a+simd")
string(APPEND CMAKE_CXX_FLAGS_INIT  " -march=armv8.5-a+simd")

include(${CMAKE_CURRENT_LIST_DIR}/flags-gcc.cmake)
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake toolchain file defining Clang compiler flags
#          for 64-bit x86 targets.
#
# Authors: David Hrdlička, <hrdlickadavid@outlook.com>
#          dob205
#
#          Copyright 2021 David Hrdlička.
#          Copyright 2022 dob205.
#

include(${CMAKE_CURRENT_LIST_DIR}/flags-gcc-x86_64.cmake)

# Use the GCC-compatible Clang executables in order to use our flags
set(CMAKE_C_COMPILER    clang)
set(CMAKE_CXX_COMPILER  clang++)

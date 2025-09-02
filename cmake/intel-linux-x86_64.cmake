#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake toolchain file defining GCC compiler flags
#          for 64-bit x86 targets.
#
# Authors: David Hrdlička, <hrdlickadavid@outlook.com>
#
#          Copyright 2021 David Hrdlička.
#

include(${CMAKE_CURRENT_LIST_DIR}/flags-gcc-x86_64.cmake)

set(CMAKE_C_COMPILER    icx)
set(CMAKE_CXX_COMPILER  icpx)

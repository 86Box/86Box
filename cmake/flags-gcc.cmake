#
# 86Box     A hypervisor and IBM PC system emulator that specializes in
#           running old operating systems and software designed for IBM
#           PC systems and compatibles from 1981 through fairly recent
#           system designs based on the PCI bus.
#
#           This file is part of the 86Box distribution.
#
#           CMake toolchain file defining GCC compiler flags.
#
# Authors:  David Hrdlička, <hrdlickadavid@outlook.com>
#
#           Copyright 2021 David Hrdlička.
#

set(CMAKE_CONFIGURATION_TYPES       Debug;Release;Optimized)

set(CMAKE_C_FLAGS_INIT              "-fomit-frame-pointer -mstackrealign -Wall -fno-strict-aliasing")
set(CMAKE_CXX_FLAGS_INIT            ${CMAKE_C_FLAGS_INIT})
set(CMAKE_C_FLAGS_RELEASE_INIT      "-g0 -O3")
set(CMAKE_CXX_FLAGS_RELEASE_INIT    ${CMAKE_C_FLAGS_RELEASE_INIT})
set(CMAKE_C_FLAGS_DEBUG_INIT        "-ggdb -Og")
set(CMAKE_CXX_FLAGS_DEBUG_INIT      ${CMAKE_C_FLAGS_DEBUG_INIT})
set(CMAKE_C_FLAGS_OPTIMIZED_INIT    "-march=native -mtune=native -O3 -ffp-contract=fast -flto")
set(CMAKE_CXX_FLAGS_OPTIMIZED_INIT  ${CMAKE_C_FLAGS_OPTIMIZED_INIT})
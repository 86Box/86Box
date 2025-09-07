#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake toolchain file defining GCC compiler flags.
#
# Authors: David Hrdlička, <hrdlickadavid@outlook.com>
#
#          Copyright 2021 David Hrdlička.
#

# Define our flags
string(APPEND CMAKE_C_FLAGS_INIT                " -fomit-frame-pointer -Wall -fno-strict-aliasing -Werror=implicit-int -Werror=implicit-function-declaration -Werror=int-conversion -Werror=strict-prototypes -Werror=old-style-definition")
string(APPEND CMAKE_CXX_FLAGS_INIT              " -fomit-frame-pointer -Wall -fno-strict-aliasing")
string(APPEND CMAKE_C_FLAGS_RELEASE_INIT        " -g0 -O3")
string(APPEND CMAKE_CXX_FLAGS_RELEASE_INIT      " -g0 -O3")
string(APPEND CMAKE_C_FLAGS_DEBUG_INIT          " -ggdb -Og")
string(APPEND CMAKE_CXX_FLAGS_DEBUG_INIT        " -ggdb -Og")
string(APPEND CMAKE_C_FLAGS_ULTRADEBUG_INIT     " -O0 -ggdb -g3")
string(APPEND CMAKE_CXX_FLAGS_ULTRADEBUG_INIT   " -O0 -ggdb -g3")
string(APPEND CMAKE_C_FLAGS_OPTIMIZED_INIT      " -march=native -mtune=native -O3 -ffp-contract=fast -flto")
string(APPEND CMAKE_CXX_FLAGS_OPTIMIZED_INIT    " -march=native -mtune=native -O3 -ffp-contract=fast -flto")

# Set up the variables
foreach(LANG C;CXX)
    set(CMAKE_${LANG}_FLAGS "$ENV{${LANG}FLAGS} ${CMAKE_${LANG}_FLAGS_INIT}" CACHE STRING "Flags used by the ${LANG} compiler during all build types.")
    mark_as_advanced(CMAKE_${LANG}_FLAGS)

    foreach(CONFIG RELEASE;DEBUG;ULTRADEBUG;OPTIMIZED)
        set(CMAKE_${LANG}_FLAGS_${CONFIG} "${CMAKE_${LANG}_FLAGS_${CONFIG}_INIT}" CACHE STRING "Flags used by the ${LANG} compiler during ${CONFIG} builds.")
        mark_as_advanced(CMAKE_${LANG}_FLAGS_${CONFIG})
    endforeach()
endforeach()

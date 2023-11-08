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
#
#          Copyright 2020-2021 David Hrdlička.
#

if(DYNAREC)
    add_library(dynarec OBJECT codegen.c codegen_ops.c)

    if(ARCH STREQUAL "i386")
        target_sources(dynarec PRIVATE codegen_x86.c
            codegen_accumulate_x86.c)
    elseif(ARCH STREQUAL "x86_64")
        target_sources(dynarec PRIVATE codegen_x86-64.c
            codegen_accumulate_x86-64.c)
    else()
        message(SEND_ERROR
            "Dynarec is incompatible with target platform ${ARCH}")
    endif()

    target_link_libraries(86Box dynarec cgt)
endif()

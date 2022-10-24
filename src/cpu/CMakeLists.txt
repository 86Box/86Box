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

add_library(cpu OBJECT cpu.c cpu_table.c fpu.c x86.c 808x.c 386.c 386_common.c
    386_dynarec.c x86seg.c x87.c x87_timings.c 8080.c)

if(AMD_K5)
    target_compile_definitions(cpu PRIVATE USE_AMD_K5)
endif()

if(CYRIX_6X86)
    target_compile_definitions(cpu PRIVATE USE_CYRIX_6X86)
endif()

if(DYNAREC)
    target_sources(cpu PRIVATE 386_dynarec_ops.c)

    add_library(cgt OBJECT codegen_timing_486.c codegen_timing_686.c
        codegen_timing_common.c codegen_timing_k6.c
        codegen_timing_pentium.c codegen_timing_p6.c
        codegen_timing_winchip.c codegen_timing_winchip2.c)
endif()

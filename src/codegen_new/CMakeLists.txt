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
    add_library(dynarec OBJECT codegen.c codegen_accumulate.c
        codegen_allocator.c codegen_block.c codegen_ir.c codegen_ops.c
        codegen_ops_3dnow.c codegen_ops_branch.c codegen_ops_arith.c
        codegen_ops_fpu_arith.c codegen_ops_fpu_constant.c
        codegen_ops_fpu_loadstore.c codegen_ops_fpu_misc.c
        codegen_ops_helpers.c codegen_ops_jump.c codegen_ops_logic.c
        codegen_ops_misc.c codegen_ops_mmx_arith.c codegen_ops_mmx_cmp.c
        codegen_ops_mmx_loadstore.c codegen_ops_mmx_logic.c
        codegen_ops_mmx_pack.c codegen_ops_mmx_shift.c codegen_ops_mov.c
        codegen_ops_shift.c codegen_ops_stack.c codegen_reg.c)

    if(ARCH STREQUAL "i386")
        target_sources(dynarec PRIVATE codegen_backend_x86.c
            codegen_backend_x86_ops.c codegen_backend_x86_ops_fpu.c
            codegen_backend_x86_ops_sse.c
            codegen_backend_x86_uops.c)
    elseif(ARCH STREQUAL "x86_64")
        target_sources(dynarec PRIVATE codegen_backend_x86-64.c
            codegen_backend_x86-64_ops.c
            codegen_backend_x86-64_ops_sse.c
            codegen_backend_x86-64_uops.c)
    elseif(ARCH STREQUAL "arm64")
        target_sources(dynarec PRIVATE codegen_backend_arm64.c
            codegen_backend_arm64_ops.c codegen_backend_arm64_uops.c
            codegen_backend_arm64_imm.c)
    elseif(ARCH STREQUAL "arm")
        target_sources(dynarec PRIVATE codegen_backend_arm.c
            codegen_backend_arm_ops.c codegen_backend_arm_uops.c)
    else()
        message(SEND_ERROR
            "Dynarec is incompatible with target platform ${ARCH}")
    endif()

    target_link_libraries(86Box dynarec cgt)
endif()

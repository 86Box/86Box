#if defined __loongarch_lp64

/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          LoongArch64 backend for the "new" dynamic recompiler -
 *          primitive emitters and the codegen_direct_* accessors.
 *
 *          Skeleton (milestone M0): the emitters exist so the generic
 *          layers link, but every one of them fatal()s - the real
 *          instruction emitters (plan sections 7 and 8) land in M1.
 */

#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat_unused.h>

#    include "codegen.h"
#    include "codegen_backend.h"
#    include "codegen_backend_loongarch64_defs.h"
#    include "codegen_ir_defs.h"

/*Reads from cpu_state / the host frame.*/

void
codegen_direct_read_8(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(void *p))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_read_16(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(void *p))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_read_32(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(void *p))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_read_64(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(void *p))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_read_pointer(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(void *p))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_read_double(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(void *p))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_read_st_8(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(void *base), UNUSED(int reg_idx))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_read_st_64(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(void *base), UNUSED(int reg_idx))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_read_st_double(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(void *base), UNUSED(int reg_idx))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_read_16_stack(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(int stack_offset))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_read_32_stack(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(int stack_offset))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_read_64_stack(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(int stack_offset))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_read_pointer_stack(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(int stack_offset))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_read_double_stack(UNUSED(codeblock_t *block), UNUSED(int host_reg), UNUSED(int stack_offset))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

/*Writes to cpu_state / the host frame.*/

void
codegen_direct_write_8(UNUSED(codeblock_t *block), UNUSED(void *p), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_16(UNUSED(codeblock_t *block), UNUSED(void *p), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_32(UNUSED(codeblock_t *block), UNUSED(void *p), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_64(UNUSED(codeblock_t *block), UNUSED(void *p), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_pointer(UNUSED(codeblock_t *block), UNUSED(void *p), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_ptr(UNUSED(codeblock_t *block), UNUSED(void *p), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_double(UNUSED(codeblock_t *block), UNUSED(void *p), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_st_8(UNUSED(codeblock_t *block), UNUSED(void *base), UNUSED(int reg_idx), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_st_64(UNUSED(codeblock_t *block), UNUSED(void *base), UNUSED(int reg_idx), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_st_double(UNUSED(codeblock_t *block), UNUSED(void *base), UNUSED(int reg_idx), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_32_stack(UNUSED(codeblock_t *block), UNUSED(int stack_offset), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_64_stack(UNUSED(codeblock_t *block), UNUSED(int stack_offset), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_pointer_stack(UNUSED(codeblock_t *block), UNUSED(int stack_offset), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_double_stack(UNUSED(codeblock_t *block), UNUSED(int stack_offset), UNUSED(int host_reg))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

/*Immediate stores (enabled by CODEGEN_BACKEND_HAS_MOV_IMM).*/

void
codegen_direct_write_8_imm(UNUSED(codeblock_t *block), UNUSED(void *p), UNUSED(uint8_t imm_data))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_16_imm(UNUSED(codeblock_t *block), UNUSED(void *p), UNUSED(uint16_t imm_data))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_32_imm(UNUSED(codeblock_t *block), UNUSED(void *p), UNUSED(uint32_t imm_data))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

void
codegen_direct_write_32_imm_stack(UNUSED(codeblock_t *block), UNUSED(int stack_offset), UNUSED(uint32_t imm_data))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

/*Branch patch-up used by codegen_ir_compile().*/

void
codegen_set_jump_dest(UNUSED(codeblock_t *block), UNUSED(void *p))
{
    fatal("codegen_backend_loongarch64_ops: not implemented yet\n");
}

#endif

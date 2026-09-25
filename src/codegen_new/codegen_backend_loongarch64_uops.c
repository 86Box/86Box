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
 *          uop handlers (milestone M1: integer core + control flow).
 *
 *          Handlers mirror the arm64 backend's semantics; every uop not
 *          in the M1 set dispatches to a fatal() stub so gaps surface
 *          immediately on the target machine (plan section 13.3).
 */

#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat_unused.h>

#    include "x86.h"
#    include "x86seg_common.h"
#    include "x86seg.h"
#    include "x87_sf.h"
#    include "x87.h"
#    include "386_common.h"
#    include "codegen.h"
#    include "codegen_backend.h"
#    include "codegen_backend_loongarch64_defs.h"
#    include "codegen_backend_loongarch64.h"
#    include "codegen_ir_defs.h"

#    define HOST_REG_GET(reg) (IREG_GET_REG(reg) &0x1f)

#    define REG_IS_L(size)  (size == IREG_SIZE_L)
#    define REG_IS_W(size)  (size == IREG_SIZE_W)
#    define REG_IS_B(size)  (size == IREG_SIZE_B)
#    define REG_IS_BH(size) (size == IREG_SIZE_BH)
#    define REG_IS_D(size)  (size == IREG_SIZE_D)
#    define REG_IS_Q(size)  (size == IREG_SIZE_Q)

static int
codegen_UOP_UNIMPLEMENTED(codeblock_t *block, uop_t *uop)
{
    fatal("codegen_backend_loongarch64_uops: uop %08x not implemented yet\n", uop->type);
    return 0;
}

static int
codegen_ADD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_loong64_ADD_W_REG(block, dest_reg, src_reg_a, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_loong64_ADD_W_REG(block, REG_TEMP, src_reg_a, src_reg_b);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_loong64_ADD_W_REG(block, REG_TEMP, src_reg_a, src_reg_b);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_BH(src_size_b)) {
        host_loong64_SHR_W_IMM(block, REG_TEMP, src_reg_b, 8);
        host_loong64_ADD_W_REG(block, REG_TEMP, src_reg_a, REG_TEMP);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b)) {
        host_loong64_SHR_W_IMM(block, REG_TEMP, src_reg_a, 8);
        host_loong64_ADD_W_REG(block, REG_TEMP, src_reg_b, REG_TEMP);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b)) {
        host_loong64_ANDI(block, REG_TEMP, src_reg_a, 0xff00);
        host_loong64_ADD_W_REG(block, REG_TEMP, REG_TEMP, src_reg_b);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("ADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    return 0;
}

static int
codegen_ADD_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_loong64_ADD_W_IMM(block, dest_reg, src_reg, (uint32_t) uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_loong64_ADD_W_IMM(block, REG_TEMP, src_reg, (uint32_t) uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_loong64_ADD_W_IMM(block, REG_TEMP, src_reg, (uint32_t) uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_B(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_ADD_W_IMM(block, REG_TEMP, src_reg, (uint32_t) uop->imm_data << 8);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_ADD_W_IMM(block, REG_TEMP, src_reg, (uint32_t) uop->imm_data << 8);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("ADD_IMM %x %x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}

static int
codegen_ADD_LSHIFT(codeblock_t *block, uop_t *uop)
{
    /*dst = src_a + (src_b << imm); alsl.d covers shifts 1..4, plain add
      for the shift-0 case (mirrors arm64's ADD_REG LSL form).*/
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b = HOST_REG_GET(uop->src_reg_b_real);

    if (uop->imm_data)
        host_loong64_ALSL_D(block, dest_reg, src_reg_a, src_reg_b, (int) uop->imm_data);
    else
        host_loong64_ADDX_REG(block, dest_reg, src_reg_a, src_reg_b);
    return 0;
}

static int
codegen_AND(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_loong64_AND_REG(block, dest_reg, src_reg_a, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_loong64_AND_REG(block, REG_TEMP, src_reg_a, src_reg_b);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_loong64_AND_REG(block, REG_TEMP, src_reg_a, src_reg_b);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_BH(src_size_b)) {
        host_loong64_SHR_W_IMM(block, REG_TEMP, src_reg_b, 8);
        host_loong64_AND_REG(block, REG_TEMP, src_reg_a, REG_TEMP);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_B(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b)) {
        host_loong64_SHR_W_IMM(block, REG_TEMP, src_reg_a, 8);
        host_loong64_AND_REG(block, REG_TEMP, src_reg_b, REG_TEMP);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_B(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b)) {
        host_loong64_AND_REG(block, REG_TEMP, src_reg_a, src_reg_b);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b)) {
        host_loong64_SHL_D_IMM(block, REG_TEMP, src_reg_b, 8);
        host_loong64_AND_REG(block, REG_TEMP, src_reg_a, REG_TEMP);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_B(src_size_a) && REG_IS_BH(src_size_b)) {
        host_loong64_SHR_W_IMM(block, REG_TEMP, src_reg_b, 8);
        host_loong64_AND_REG(block, REG_TEMP, src_reg_a, REG_TEMP);
        host_loong64_SHL_D_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b)) {
        host_loong64_AND_REG(block, REG_TEMP, src_reg_a, src_reg_b);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("AND %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    return 0;
}

static int
codegen_AND_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_loong64_AND_IMM(block, dest_reg, src_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        /*Keep bits [31:16] of the destination: AND with imm | ones.*/
        host_loong64_AND_IMM(block, dest_reg, src_reg, uop->imm_data | 0xffff0000ull);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_loong64_AND_IMM(block, dest_reg, src_reg, uop->imm_data | 0xffffff00ull);
    } else if (REG_IS_B(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_SHR_W_IMM(block, REG_TEMP, src_reg, 8);
        host_loong64_AND_IMM(block, REG_TEMP, REG_TEMP, uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_AND_IMM(block, dest_reg, src_reg, (uop->imm_data << 8) | 0xffff00ffull);
    } else
        fatal("AND_IMM %x %x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}

static int
codegen_CALL_FUNC(codeblock_t *block, uop_t *uop)
{
    host_loong64_call(block, uop->p);

    return 0;
}

static int
codegen_CALL_FUNC_RESULT(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (!REG_IS_L(dest_size))
        fatal("CALL_FUNC_RESULT %02x\n", uop->dest_reg_a_real);
    host_loong64_call(block, uop->p);
    host_loong64_MOV_REG(block, dest_reg, REG_A0);

    return 0;
}

static int
codegen_CALL_INSTRUCTION_FUNC(codeblock_t *block, uop_t *uop)
{
    host_loong64_mov_imm(block, REG_ARG0, uop->imm_data);
    host_loong64_call(block, uop->p);
    host_loong64_branch_reg_ne(block, REG_A0, REG_ZERO, codegen_exit_rout);

    return 0;
}

static int
codegen_CMP_IMM_JZ(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        /*32-bit equality with no flags reg: canonicalise both sides to
          sign-extended 32-bit form, then xor and test for zero. The
          canonicalisation is required because 32-bit values may be held
          zero-extended (loads) or sign-extended (.w ops).*/
        host_loong64_MOV_W(block, REG_TEMP, src_reg);
        host_loong64_mov_imm_w(block, REG_TEMP2, (uint32_t) uop->imm_data);
        host_loong64_XOR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
    } else
        fatal("CMP_IMM_JZ %02x\n", uop->src_reg_a_real);
    host_loong64_branch_reg_eq(block, REG_TEMP, REG_ZERO, uop->p);

    return 0;
}

static int
codegen_JMP(codeblock_t *block, uop_t *uop)
{
    host_loong64_jump(block, (uintptr_t) uop->p);

    return 0;
}

static int
codegen_LOAD_FUNC_ARG0(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_W(src_size)) {
        host_loong64_ANDI(block, REG_ARG0, src_reg, 0xffff);
    } else
        fatal("codegen_LOAD_FUNC_ARG0 %02x\n", uop->src_reg_a_real);

    return 0;
}
static int
codegen_LOAD_FUNC_ARG1(codeblock_t *block, uop_t *uop)
{
    fatal("codegen_LOAD_FUNC_ARG1 %02x\n", uop->src_reg_a_real);
    return 0;
}
static int
codegen_LOAD_FUNC_ARG2(codeblock_t *block, uop_t *uop)
{
    fatal("codegen_LOAD_FUNC_ARG2 %02x\n", uop->src_reg_a_real);
    return 0;
}
static int
codegen_LOAD_FUNC_ARG3(codeblock_t *block, uop_t *uop)
{
    fatal("codegen_LOAD_FUNC_ARG3 %02x\n", uop->src_reg_a_real);
    return 0;
}

static int
codegen_LOAD_FUNC_ARG0_IMM(codeblock_t *block, uop_t *uop)
{
    host_loong64_MOVX_IMM(block, REG_ARG0, uop->imm_data);

    return 0;
}
static int
codegen_LOAD_FUNC_ARG1_IMM(codeblock_t *block, uop_t *uop)
{
    host_loong64_MOVX_IMM(block, REG_ARG1, uop->imm_data);

    return 0;
}
static int
codegen_LOAD_FUNC_ARG2_IMM(codeblock_t *block, uop_t *uop)
{
    host_loong64_MOVX_IMM(block, REG_ARG2, uop->imm_data);

    return 0;
}
static int
codegen_LOAD_FUNC_ARG3_IMM(codeblock_t *block, uop_t *uop)
{
    host_loong64_MOVX_IMM(block, REG_ARG3, uop->imm_data);

    return 0;
}

static int
codegen_LOAD_SEG(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (!REG_IS_W(src_size))
        fatal("LOAD_SEG %02x %p\n", uop->src_reg_a_real, uop->p);

    host_loong64_MOVX_IMM(block, REG_ARG1, (uint64_t) (uintptr_t) uop->p);
    host_loong64_ANDI(block, REG_ARG0, src_reg, 0xffff);
    host_loong64_call(block, (void *) loadseg);
    host_loong64_branch_reg_ne(block, REG_A0, REG_ZERO, codegen_exit_rout);

    return 0;
}

static int
codegen_MEM_LOAD_ABS(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    host_loong64_ADD_W_IMM(block, REG_A0, seg_reg, (uint32_t) uop->imm_data);
    /*32-bit guest address - canonicalise to zero-extended so the stub's
      page calculation (srli.d by 12) sees the right page number.*/
    host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 32);
    if (REG_IS_B(dest_size) || REG_IS_BH(dest_size)) {
        host_loong64_call(block, codegen_mem_load_byte);
    } else if (REG_IS_W(dest_size)) {
        host_loong64_call(block, codegen_mem_load_word);
    } else if (REG_IS_L(dest_size)) {
        host_loong64_call(block, codegen_mem_load_long);
    } else
        fatal("MEM_LOAD_ABS - %02x\n", uop->dest_reg_a_real);
    host_loong64_branch_reg_ne(block, REG_A1, REG_ZERO, codegen_exit_rout);
    if (REG_IS_B(dest_size)) {
        host_loong64_BFI_W(block, dest_reg, REG_A0, 0, 8);
    } else if (REG_IS_BH(dest_size)) {
        host_loong64_BFI_W(block, dest_reg, REG_A0, 8, 8);
    } else if (REG_IS_W(dest_size)) {
        host_loong64_BFI_W(block, dest_reg, REG_A0, 0, 16);
    } else if (REG_IS_L(dest_size)) {
        host_loong64_MOV_REG(block, dest_reg, REG_A0);
    }

    return 0;
}
static int
codegen_MEM_LOAD_REG(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    host_loong64_ADDX_REG(block, REG_A0, seg_reg, addr_reg);
    if (uop->imm_data)
        host_loong64_ADD_W_IMM(block, REG_A0, REG_A0, (uint32_t) uop->imm_data);
    if (uop->is_a16)
        host_loong64_ANDI(block, REG_A0, REG_A0, 0xffff);
    else
        host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 32);
    if (REG_IS_B(dest_size) || REG_IS_BH(dest_size)) {
        host_loong64_call(block, codegen_mem_load_byte);
    } else if (REG_IS_W(dest_size)) {
        host_loong64_call(block, codegen_mem_load_word);
    } else if (REG_IS_L(dest_size)) {
        host_loong64_call(block, codegen_mem_load_long);
    } else if (REG_IS_Q(dest_size)) {
        host_loong64_call(block, codegen_mem_load_quad);
    } else
        fatal("MEM_LOAD_REG - %02x\n", uop->dest_reg_a_real);
    host_loong64_branch_reg_ne(block, REG_A1, REG_ZERO, codegen_exit_rout);
    if (REG_IS_B(dest_size)) {
        host_loong64_BFI_W(block, dest_reg, REG_A0, 0, 8);
    } else if (REG_IS_BH(dest_size)) {
        host_loong64_BFI_W(block, dest_reg, REG_A0, 8, 8);
    } else if (REG_IS_W(dest_size)) {
        host_loong64_BFI_W(block, dest_reg, REG_A0, 0, 16);
    } else if (REG_IS_L(dest_size)) {
        host_loong64_MOV_REG(block, dest_reg, REG_A0);
    } else if (REG_IS_Q(dest_size)) {
        host_loong64_VMOV_F(block, dest_reg, REG_V_TEMP);
    }

    return 0;
}

static int
codegen_MEM_STORE_ABS(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_b_real);

    host_loong64_ADD_W_IMM(block, REG_A0, seg_reg, (uint32_t) uop->imm_data);
    host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 32);
    if (REG_IS_B(src_size)) {
        host_loong64_ANDI(block, REG_A1, src_reg, 0xff);
        host_loong64_call(block, codegen_mem_store_byte);
    } else if (REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, REG_A1, src_reg, 8, 8);
        host_loong64_call(block, codegen_mem_store_byte);
    } else if (REG_IS_W(src_size)) {
        host_loong64_ANDI(block, REG_A1, src_reg, 0xffff);
        host_loong64_call(block, codegen_mem_store_word);
    } else if (REG_IS_L(src_size)) {
        host_loong64_MOV_REG(block, REG_A1, src_reg);
        host_loong64_call(block, codegen_mem_store_long);
    } else
        fatal("MEM_STORE_ABS - %02x\n", uop->dest_reg_a_real);
    host_loong64_branch_reg_ne(block, REG_A1, REG_ZERO, codegen_exit_rout);

    return 0;
}
static int
codegen_MEM_STORE_REG(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);
    int src_reg  = HOST_REG_GET(uop->src_reg_c_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_c_real);

    host_loong64_ADD_W_REG(block, REG_A0, seg_reg, addr_reg);
    if (uop->imm_data)
        host_loong64_ADD_W_IMM(block, REG_A0, REG_A0, (uint32_t) uop->imm_data);
    if (uop->is_a16)
        host_loong64_ANDI(block, REG_A0, REG_A0, 0xffff);
    else
        host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 32);
    if (REG_IS_B(src_size)) {
        host_loong64_ANDI(block, REG_A1, src_reg, 0xff);
        host_loong64_call(block, codegen_mem_store_byte);
    } else if (REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, REG_A1, src_reg, 8, 8);
        host_loong64_call(block, codegen_mem_store_byte);
    } else if (REG_IS_W(src_size)) {
        host_loong64_ANDI(block, REG_A1, src_reg, 0xffff);
        host_loong64_call(block, codegen_mem_store_word);
    } else if (REG_IS_L(src_size)) {
        host_loong64_MOV_REG(block, REG_A1, src_reg);
        host_loong64_call(block, codegen_mem_store_long);
    } else if (REG_IS_Q(src_size)) {
        host_loong64_VMOV_F(block, REG_V_TEMP, src_reg);
        host_loong64_call(block, codegen_mem_store_quad);
    } else
        fatal("MEM_STORE_REG - %02x\n", uop->src_reg_c_real);
    host_loong64_branch_reg_ne(block, REG_A1, REG_ZERO, codegen_exit_rout);

    return 0;
}

static int
codegen_MOV(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_loong64_MOV_REG(block, dest_reg, src_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_loong64_BFI_W(block, dest_reg, src_reg, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_loong64_BFI_W(block, dest_reg, src_reg, 0, 8);
    } else if (REG_IS_B(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 8, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_B(src_size)) {
        host_loong64_BFI_W(block, dest_reg, src_reg, 8, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 8, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else if (REG_IS_W(dest_size) && REG_IS_L(src_size)) {
        /*Preserve upper destination bits; only replace the low 16 bits.*/
        host_loong64_BFI_W(block, dest_reg, src_reg, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_L(src_size)) {
        host_loong64_BFI_W(block, dest_reg, src_reg, 0, 8);
    } else if (REG_IS_B(dest_size) && REG_IS_W(src_size)) {
        host_loong64_BFI_W(block, dest_reg, src_reg, 0, 8);
    } else
        fatal("MOV %x %x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_MOV_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_L(dest_size)) {
        host_loong64_mov_imm(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size)) {
        host_loong64_mov_imm_w(block, REG_TEMP, (uint32_t) uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size)) {
        host_loong64_ORI(block, REG_TEMP, REG_ZERO, (uint32_t) uop->imm_data & 0xff);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size)) {
        host_loong64_ORI(block, REG_TEMP, REG_ZERO, (uint32_t) uop->imm_data & 0xff);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("MOV_IMM %x\n", uop->dest_reg_a_real);

    return 0;
}
static int
codegen_MOV_PTR(codeblock_t *block, uop_t *uop)
{
    host_loong64_MOVX_IMM(block, HOST_REG_GET(uop->dest_reg_a_real), (uint64_t) (uintptr_t) uop->p);

    return 0;
}

static int
codegen_MOVSX(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_B(src_size)) {
        host_loong64_SEXT_B(block, dest_reg, src_reg);
    } else if (REG_IS_L(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 8, 8);
        host_loong64_SEXT_B(block, dest_reg, REG_TEMP);
    } else if (REG_IS_L(dest_size) && REG_IS_W(src_size)) {
        host_loong64_SEXT_H(block, dest_reg, src_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_B(src_size)) {
        host_loong64_SEXT_B(block, REG_TEMP, src_reg);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_W(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 8, 8);
        host_loong64_SEXT_B(block, REG_TEMP, REG_TEMP);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else
        fatal("MOVSX %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_MOVZX(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_Q(dest_size) && REG_IS_L(src_size)) {
        host_loong64_MOVGR2FR_D(block, dest_reg, src_reg);
    } else if (REG_IS_L(dest_size) && REG_IS_Q(src_size)) {
        host_loong64_MOVFR2GR_S(block, dest_reg, src_reg);
    } else if (REG_IS_L(dest_size) && REG_IS_B(src_size)) {
        host_loong64_ANDI(block, dest_reg, src_reg, 0xff);
    } else if (REG_IS_L(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, dest_reg, src_reg, 8, 8);
    } else if (REG_IS_L(dest_size) && REG_IS_W(src_size)) {
        host_loong64_ANDI(block, dest_reg, src_reg, 0xffff);
    } else if (REG_IS_W(dest_size) && REG_IS_B(src_size)) {
        host_loong64_ANDI(block, REG_TEMP, src_reg, 0xff);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_W(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 8, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else
        fatal("MOVZX %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}

static int
codegen_MOV_REG_PTR(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    host_loong64_MOVX_IMM(block, REG_TEMP, (uint64_t) (uintptr_t) uop->p);
    if (REG_IS_L(dest_size)) {
        host_loong64_LDR_W_IMM(block, dest_reg, REG_TEMP, 0);
    } else
        fatal("MOV_REG_PTR %02x\n", uop->dest_reg_a_real);

    return 0;
}
static int
codegen_MOVZX_REG_PTR_8(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    host_loong64_MOVX_IMM(block, REG_TEMP, (uint64_t) (uintptr_t) uop->p);
    if (REG_IS_L(dest_size)) {
        host_loong64_LDRB_REG(block, dest_reg, REG_TEMP, REG_ZERO);
    } else if (REG_IS_W(dest_size)) {
        host_loong64_LDRB_REG(block, REG_TEMP, REG_TEMP, REG_ZERO);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size)) {
        host_loong64_LDRB_REG(block, REG_TEMP, REG_TEMP, REG_ZERO);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else
        fatal("MOVZX_REG_PTR_8 %02x\n", uop->dest_reg_a_real);

    return 0;
}
static int
codegen_MOVZX_REG_PTR_16(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    host_loong64_MOVX_IMM(block, REG_TEMP, (uint64_t) (uintptr_t) uop->p);
    if (REG_IS_L(dest_size)) {
        host_loong64_LDRH_REG(block, dest_reg, REG_TEMP, REG_ZERO);
    } else if (REG_IS_W(dest_size)) {
        host_loong64_LDRH_REG(block, REG_TEMP, REG_TEMP, REG_ZERO);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else
        fatal("MOVZX_REG_PTR_16 %02x\n", uop->dest_reg_a_real);

    return 0;
}

static int
codegen_NOP(codeblock_t *block, uop_t *uop)
{
    return 0;
}

#ifdef DEBUG_EXTRA
static int
codegen_LOG_INSTR(codeblock_t *block, uop_t *uop)
{
    /*UOP_LOG_INSTR only exists in DEBUG_EXTRA builds and only logs; the
      interpreter-side logging covers it, so treat it as a barrier-only
      no-op (arm64 does not register it at all).*/
    return 0;
}
#endif

static int
codegen_OR(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_loong64_OR_REG(block, dest_reg, src_reg_a, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_loong64_OR_REG(block, REG_TEMP, src_reg_a, src_reg_b);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b) && dest_reg == src_reg_a) {
        host_loong64_ANDI(block, REG_TEMP, src_reg_b, 0xff);
        host_loong64_OR_REG(block, dest_reg, src_reg_a, REG_TEMP);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_BH(src_size_b) && dest_reg == src_reg_a) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg_b, 8, 8);
        host_loong64_OR_REG(block, dest_reg, src_reg_a, REG_TEMP);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b) && dest_reg == src_reg_a) {
        host_loong64_ANDI(block, REG_TEMP, src_reg_b, 0xff);
        host_loong64_SHL_D_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_OR_REG(block, dest_reg, src_reg_a, REG_TEMP);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b) && dest_reg == src_reg_a) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg_b, 8, 8);
        host_loong64_SHL_D_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_OR_REG(block, dest_reg, src_reg_a, REG_TEMP);
    } else
        fatal("OR %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    return 0;
}
static int
codegen_OR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_loong64_OR_IMM(block, dest_reg, src_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size) && dest_reg == src_reg) {
        host_loong64_OR_IMM(block, dest_reg, src_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size) && dest_reg == src_reg) {
        host_loong64_OR_IMM(block, dest_reg, src_reg, uop->imm_data);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size) && dest_reg == src_reg) {
        host_loong64_OR_IMM(block, dest_reg, src_reg, uop->imm_data << 8);
    } else
        fatal("OR_IMM %x %x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}

static int
codegen_SUB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_loong64_SUB_W_REG(block, dest_reg, src_reg_a, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_loong64_SUB_W_REG(block, REG_TEMP, src_reg_a, src_reg_b);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_loong64_SUB_W_REG(block, REG_TEMP, src_reg_a, src_reg_b);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_BH(src_size_b)) {
        host_loong64_SHR_W_IMM(block, REG_TEMP, src_reg_b, 8);
        host_loong64_SUB_W_REG(block, REG_TEMP, src_reg_a, REG_TEMP);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_B(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b)) {
        host_loong64_SHL_D_IMM(block, REG_TEMP, src_reg_b, 8);
        host_loong64_SUB_W_REG(block, REG_TEMP, src_reg_a, REG_TEMP);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_B(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b)) {
        host_loong64_SHR_W_IMM(block, REG_TEMP, src_reg_a, 8);
        host_loong64_SHR_W_IMM(block, REG_TEMP2, src_reg_b, 8);
        host_loong64_SUB_W_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b)) {
        host_loong64_SHL_D_IMM(block, REG_TEMP, src_reg_b, 8);
        host_loong64_SUB_W_REG(block, REG_TEMP, src_reg_a, REG_TEMP);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b)) {
        host_loong64_SHR_W_IMM(block, REG_TEMP, src_reg_a, 8);
        host_loong64_SHR_W_IMM(block, REG_TEMP2, src_reg_b, 8);
        host_loong64_SUB_W_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("SUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    return 0;
}

static int
codegen_SUB_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_loong64_SUB_W_IMM(block, dest_reg, src_reg, (uint32_t) uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_loong64_SUB_W_IMM(block, REG_TEMP, src_reg, (uint32_t) uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_loong64_SUB_W_IMM(block, REG_TEMP, src_reg, (uint32_t) uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_B(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_SUB_W_IMM(block, REG_TEMP, src_reg, (uint32_t) uop->imm_data << 8);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_SUB_W_IMM(block, REG_TEMP, src_reg, (uint32_t) uop->imm_data << 8);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("SUB_IMM %x %x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}

static int
codegen_XOR(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_loong64_XOR_REG(block, dest_reg, src_reg_a, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b) && dest_reg == src_reg_a) {
        host_loong64_ANDI(block, REG_TEMP, src_reg_b, 0xffff);
        host_loong64_XOR_REG(block, dest_reg, src_reg_a, REG_TEMP);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b) && dest_reg == src_reg_a) {
        host_loong64_ANDI(block, REG_TEMP, src_reg_b, 0xff);
        host_loong64_XOR_REG(block, dest_reg, src_reg_a, REG_TEMP);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_BH(src_size_b) && dest_reg == src_reg_a) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg_b, 8, 8);
        host_loong64_XOR_REG(block, dest_reg, src_reg_a, REG_TEMP);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b) && dest_reg == src_reg_a) {
        host_loong64_ANDI(block, REG_TEMP, src_reg_b, 0xff);
        host_loong64_SHL_D_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_XOR_REG(block, dest_reg, src_reg_a, REG_TEMP);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b) && dest_reg == src_reg_a) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg_b, 8, 8);
        host_loong64_SHL_D_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_XOR_REG(block, dest_reg, src_reg_a, REG_TEMP);
    } else
        fatal("XOR %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    return 0;
}
static int
codegen_XOR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_loong64_XOR_IMM(block, dest_reg, src_reg, (uint32_t) uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size) && dest_reg == src_reg) {
        host_loong64_XOR_IMM(block, dest_reg, src_reg, (uint32_t) uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size) && dest_reg == src_reg) {
        host_loong64_XOR_IMM(block, dest_reg, src_reg, (uint32_t) uop->imm_data);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size) && dest_reg == src_reg) {
        host_loong64_XOR_IMM(block, dest_reg, src_reg, (uint32_t) uop->imm_data << 8);
    } else
        fatal("XOR_IMM %x %x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}

const uOpFn uop_handlers[UOP_MAX] = {
    /*Any uop without a handler below lands here and fatal()s - in both
      debug and release builds (plan section 13.3).*/
    [0 ... UOP_MAX - 1] = codegen_UOP_UNIMPLEMENTED,

    [UOP_CALL_FUNC & UOP_MASK]         = codegen_CALL_FUNC,
    [UOP_CALL_FUNC_RESULT & UOP_MASK]  = codegen_CALL_FUNC_RESULT,
    [UOP_CALL_INSTRUCTION_FUNC & UOP_MASK] = codegen_CALL_INSTRUCTION_FUNC,

    [UOP_JMP & UOP_MASK] = codegen_JMP,

    [UOP_LOAD_SEG & UOP_MASK] = codegen_LOAD_SEG,

    [UOP_LOAD_FUNC_ARG_0 & UOP_MASK] = codegen_LOAD_FUNC_ARG0,
    [UOP_LOAD_FUNC_ARG_1 & UOP_MASK] = codegen_LOAD_FUNC_ARG1,
    [UOP_LOAD_FUNC_ARG_2 & UOP_MASK] = codegen_LOAD_FUNC_ARG2,
    [UOP_LOAD_FUNC_ARG_3 & UOP_MASK] = codegen_LOAD_FUNC_ARG3,

    [UOP_LOAD_FUNC_ARG_0_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG0_IMM,
    [UOP_LOAD_FUNC_ARG_1_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG1_IMM,
    [UOP_LOAD_FUNC_ARG_2_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG2_IMM,
    [UOP_LOAD_FUNC_ARG_3_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG3_IMM,

    [UOP_MEM_LOAD_ABS & UOP_MASK] = codegen_MEM_LOAD_ABS,
    [UOP_MEM_LOAD_REG & UOP_MASK] = codegen_MEM_LOAD_REG,

    [UOP_MEM_STORE_ABS & UOP_MASK] = codegen_MEM_STORE_ABS,
    [UOP_MEM_STORE_REG & UOP_MASK] = codegen_MEM_STORE_REG,

    [UOP_MOV & UOP_MASK]         = codegen_MOV,
    [UOP_MOV_PTR & UOP_MASK]     = codegen_MOV_PTR,
    [UOP_MOV_IMM & UOP_MASK]     = codegen_MOV_IMM,
    [UOP_MOVSX & UOP_MASK]       = codegen_MOVSX,
    [UOP_MOVZX & UOP_MASK]       = codegen_MOVZX,
    [UOP_MOV_REG_PTR & UOP_MASK] = codegen_MOV_REG_PTR,
    [UOP_MOVZX_REG_PTR_8 & UOP_MASK]  = codegen_MOVZX_REG_PTR_8,
    [UOP_MOVZX_REG_PTR_16 & UOP_MASK] = codegen_MOVZX_REG_PTR_16,

    [UOP_ADD & UOP_MASK]       = codegen_ADD,
    [UOP_ADD_IMM & UOP_MASK]   = codegen_ADD_IMM,
    [UOP_ADD_LSHIFT & UOP_MASK] = codegen_ADD_LSHIFT,
    [UOP_AND & UOP_MASK]       = codegen_AND,
    [UOP_AND_IMM & UOP_MASK]   = codegen_AND_IMM,
    [UOP_OR & UOP_MASK]        = codegen_OR,
    [UOP_OR_IMM & UOP_MASK]    = codegen_OR_IMM,
    [UOP_SUB & UOP_MASK]       = codegen_SUB,
    [UOP_SUB_IMM & UOP_MASK]   = codegen_SUB_IMM,
    [UOP_XOR & UOP_MASK]       = codegen_XOR,
    [UOP_XOR_IMM & UOP_MASK]   = codegen_XOR_IMM,

    [UOP_CMP_IMM_JZ & UOP_MASK] = codegen_CMP_IMM_JZ,

    [UOP_NOP_BARRIER & UOP_MASK] = codegen_NOP,

#ifdef DEBUG_EXTRA
    [UOP_LOG_INSTR & UOP_MASK] = codegen_LOG_INSTR,
#endif
};

#endif

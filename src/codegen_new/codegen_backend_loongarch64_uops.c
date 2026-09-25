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
        host_loong64_UBFX_D(block, REG_TEMP, src_reg_a, 8, 8);
        host_loong64_SHL_D_IMM(block, REG_TEMP, REG_TEMP, 8);
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
        /*alsl.d shifts its FIRST source operand (rj): rd = (rj << sa) + rk,
          while the uop semantics (and arm64's ADD_REG LSL) are
          dst = src_a + (src_b << imm) - so the shifted operand must go
          into rj and the plain one into rk.*/
        host_loong64_ALSL_D(block, dest_reg, src_reg_b, src_reg_a, (int) uop->imm_data);
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

/*Compare helpers - LA64 has no flags register, so every conditional uop
  canonicalises its operands (32-bit values may be held zero-extended or
  sign-extended) and materialises the condition in a register before
  branching (plan sections 6.1 and 8.4).*/

/*Both operands sign-extended to the uop's width (ext.w.b/h do the 8/16-bit
  forms in one instruction); returns the two registers for blt/bge-family
  compares. The signedness of the 64-bit compare then matches the width.*/
static void
cmp_sext_pair(codeblock_t *block, int size_a, int size_b, int src_a, int src_b, int *ra, int *rb)
{
    if (size_a != size_b)
        fatal("cmp_sext_pair - size mismatch\n");
    if (REG_IS_L(size_a)) {
        host_loong64_MOV_W(block, REG_TEMP, src_a);
        host_loong64_MOV_W(block, REG_TEMP2, src_b);
    } else if (REG_IS_W(size_a)) {
        host_loong64_SEXT_H(block, REG_TEMP, src_a);
        host_loong64_SEXT_H(block, REG_TEMP2, src_b);
    } else if (REG_IS_B(size_a)) {
        host_loong64_SEXT_B(block, REG_TEMP, src_a);
        host_loong64_SEXT_B(block, REG_TEMP2, src_b);
    } else
        fatal("cmp_sext_pair - bad size\n");
    *ra = REG_TEMP;
    *rb = REG_TEMP2;
}

/*Both operands zero-extended to the uop's width (unsigned compares).*/
static void
cmp_zext_pair(codeblock_t *block, int size_a, int size_b, int src_a, int src_b, int *ra, int *rb)
{
    if (size_a != size_b)
        fatal("cmp_zext_pair - size mismatch\n");
    if (REG_IS_L(size_a)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_a, 0, 32);
        host_loong64_UBFX_D(block, REG_TEMP2, src_b, 0, 32);
    } else if (REG_IS_W(size_a)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_a, 0, 16);
        host_loong64_UBFX_D(block, REG_TEMP2, src_b, 0, 16);
    } else if (REG_IS_B(size_a)) {
        host_loong64_ANDI(block, REG_TEMP, src_a, 0xff);
        host_loong64_ANDI(block, REG_TEMP2, src_b, 0xff);
    } else
        fatal("cmp_zext_pair - bad size\n");
    *ra = REG_TEMP;
    *rb = REG_TEMP2;
}

/*Register holding 0 iff the low fields of src_a and src_b are equal
  (xor + zero-extend, so mixed zext/sext upper bits cannot leak).*/
static int
cmp_equal_reg(codeblock_t *block, int size_a, int size_b, int src_a, int src_b)
{
    if (size_a != size_b)
        fatal("cmp_equal_reg - size mismatch\n");
    host_loong64_XOR_REG(block, REG_TEMP, src_a, src_b);
    if (REG_IS_L(size_a))
        host_loong64_UBFX_D(block, REG_TEMP, REG_TEMP, 0, 32);
    else if (REG_IS_W(size_a))
        host_loong64_UBFX_D(block, REG_TEMP, REG_TEMP, 0, 16);
    else if (REG_IS_B(size_a))
        host_loong64_UBFX_D(block, REG_TEMP, REG_TEMP, 0, 8);
    else
        fatal("cmp_equal_reg - bad size\n");
    return REG_TEMP;
}

/*Register whose sign bit is set iff (int-size)a - (int-size)b overflows
  (x86 CMP's OF). Operands are canonicalised to the width's signed form
  first, so the 64-bit difference is exact and the algebra is
  width-independent.*/
static int
cmp_overflow_reg(codeblock_t *block, int size_a, int size_b, int src_a, int src_b)
{
    int ra, rb;

    cmp_sext_pair(block, size_a, size_b, src_a, src_b, &ra, &rb);
    host_loong64_SUBX_REG(block, REG_TEMP3, ra, rb);
    host_loong64_XOR_REG(block, rb, ra, rb);
    host_loong64_XOR_REG(block, ra, ra, REG_TEMP3);
    host_loong64_AND_REG(block, REG_TEMP, ra, rb);
    return REG_TEMP;
}

static int
codegen_CMP_IMM_JNZ_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_loong64_MOV_W(block, REG_TEMP, src_reg);
        host_loong64_mov_imm_w(block, REG_TEMP2, (uint32_t) uop->imm_data);
        host_loong64_XOR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
    } else if (REG_IS_W(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 0, 16);
        host_loong64_mov_imm_w(block, REG_TEMP2, (uint32_t) uop->imm_data);
        host_loong64_XOR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
    } else
        fatal("CMP_IMM_JNZ_DEST %02x\n", uop->src_reg_a_real);

    uop->p = host_loong64_BNE_(block, REG_TEMP, REG_ZERO);

    return 0;
}
static int
codegen_CMP_IMM_JZ_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_loong64_MOV_W(block, REG_TEMP, src_reg);
        host_loong64_mov_imm_w(block, REG_TEMP2, (uint32_t) uop->imm_data);
        host_loong64_XOR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
    } else if (REG_IS_W(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 0, 16);
        host_loong64_mov_imm_w(block, REG_TEMP2, (uint32_t) uop->imm_data);
        host_loong64_XOR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
    } else
        fatal("CMP_IMM_JZ_DEST %02x\n", uop->src_reg_a_real);

    uop->p = host_loong64_BEQ_(block, REG_TEMP, REG_ZERO);

    return 0;
}

static int
codegen_CMP_JB(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg_a, 0, 32);
        host_loong64_UBFX_D(block, REG_TEMP2, src_reg_b, 0, 32);
        host_loong64_SLTU(block, REG_TEMP, REG_TEMP, REG_TEMP2);
    } else
        fatal("CMP_JB %02x\n", uop->src_reg_a_real);
    host_loong64_branch_reg_ne(block, REG_TEMP, REG_ZERO, uop->p);

    return 0;
}
static int
codegen_CMP_JNBE(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg_a, 0, 32);
        host_loong64_UBFX_D(block, REG_TEMP2, src_reg_b, 0, 32);
        host_loong64_SLTU(block, REG_TEMP, REG_TEMP2, REG_TEMP);
    } else
        fatal("CMP_JNBE %02x\n", uop->src_reg_a_real);
    host_loong64_branch_reg_ne(block, REG_TEMP, REG_ZERO, uop->p);

    return 0;
}

static int
codegen_CMP_JNB_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
    int ra, rb;

    cmp_zext_pair(block, src_size_a, src_size_b, src_reg_a, src_reg_b, &ra, &rb);
    uop->p = host_loong64_BGEU_(block, ra, rb);
    return 0;
}
static int
codegen_CMP_JNBE_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
    int ra, rb;

    cmp_zext_pair(block, src_size_a, src_size_b, src_reg_a, src_reg_b, &ra, &rb);
    uop->p = host_loong64_BLTU_(block, rb, ra);
    return 0;
}
static int
codegen_CMP_JNL_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
    int ra, rb;

    cmp_sext_pair(block, src_size_a, src_size_b, src_reg_a, src_reg_b, &ra, &rb);
    uop->p = host_loong64_BGE_(block, ra, rb);
    return 0;
}
static int
codegen_CMP_JNLE_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
    int ra, rb;

    cmp_sext_pair(block, src_size_a, src_size_b, src_reg_a, src_reg_b, &ra, &rb);
    uop->p = host_loong64_BLT_(block, rb, ra);
    return 0;
}
static int
codegen_CMP_JNO_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
    int t;

    t = cmp_overflow_reg(block, src_size_a, src_size_b, src_reg_a, src_reg_b);
    uop->p = host_loong64_BGE_(block, t, REG_ZERO);
    return 0;
}
static int
codegen_CMP_JNZ_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
    int t;

    t = cmp_equal_reg(block, src_size_a, src_size_b, src_reg_a, src_reg_b);
    uop->p = host_loong64_BNE_(block, t, REG_ZERO);
    return 0;
}
static int
codegen_CMP_JB_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
    int ra, rb;

    cmp_zext_pair(block, src_size_a, src_size_b, src_reg_a, src_reg_b, &ra, &rb);
    uop->p = host_loong64_BLTU_(block, ra, rb);
    return 0;
}
static int
codegen_CMP_JBE_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
    int ra, rb;

    cmp_zext_pair(block, src_size_a, src_size_b, src_reg_a, src_reg_b, &ra, &rb);
    uop->p = host_loong64_BGEU_(block, rb, ra);
    return 0;
}
static int
codegen_CMP_JL_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
    int ra, rb;

    cmp_sext_pair(block, src_size_a, src_size_b, src_reg_a, src_reg_b, &ra, &rb);
    uop->p = host_loong64_BLT_(block, ra, rb);
    return 0;
}
static int
codegen_CMP_JLE_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
    int ra, rb;

    cmp_sext_pair(block, src_size_a, src_size_b, src_reg_a, src_reg_b, &ra, &rb);
    uop->p = host_loong64_BGE_(block, rb, ra);
    return 0;
}
static int
codegen_CMP_JO_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
    int t;

    t = cmp_overflow_reg(block, src_size_a, src_size_b, src_reg_a, src_reg_b);
    uop->p = host_loong64_BLT_(block, t, REG_ZERO);
    return 0;
}
static int
codegen_CMP_JZ_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
    int t;

    t = cmp_equal_reg(block, src_size_a, src_size_b, src_reg_a, src_reg_b);
    uop->p = host_loong64_BEQ_(block, t, REG_ZERO);
    return 0;
}

static int
codegen_TEST_JNS_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    /*Move the sign bit to bit 63 and branch on the sign - one shift, no
      mask needed (blt/bge test only bit 63).*/
    if (REG_IS_L(src_size))
        host_loong64_SHL_D_IMM(block, REG_TEMP, src_reg, 32);
    else if (REG_IS_W(src_size))
        host_loong64_SHL_D_IMM(block, REG_TEMP, src_reg, 48);
    else if (REG_IS_B(src_size))
        host_loong64_SHL_D_IMM(block, REG_TEMP, src_reg, 56);
    else
        fatal("TEST_JNS_DEST %02x\n", uop->src_reg_a_real);

    uop->p = host_loong64_BGE_(block, REG_TEMP, REG_ZERO);

    return 0;
}
static int
codegen_TEST_JS_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size))
        host_loong64_SHL_D_IMM(block, REG_TEMP, src_reg, 32);
    else if (REG_IS_W(src_size))
        host_loong64_SHL_D_IMM(block, REG_TEMP, src_reg, 48);
    else if (REG_IS_B(src_size))
        host_loong64_SHL_D_IMM(block, REG_TEMP, src_reg, 56);
    else
        fatal("TEST_JS_DEST %02x\n", uop->src_reg_a_real);

    uop->p = host_loong64_BLT_(block, REG_TEMP, REG_ZERO);

    return 0;
}

static int
codegen_LOAD_FUNC_ARG0(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_W(src_size)) {
        host_loong64_UBFX_D(block, REG_ARG0, src_reg, 0, 16);
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
    host_loong64_UBFX_D(block, REG_ARG0, src_reg, 0, 16);
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
        host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 16);
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
        host_loong64_UBFX_D(block, REG_A1, src_reg, 0, 16);
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
        host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 16);
    else
        host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 32);
    if (REG_IS_B(src_size)) {
        host_loong64_ANDI(block, REG_A1, src_reg, 0xff);
        host_loong64_call(block, codegen_mem_store_byte);
    } else if (REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, REG_A1, src_reg, 8, 8);
        host_loong64_call(block, codegen_mem_store_byte);
    } else if (REG_IS_W(src_size)) {
        host_loong64_UBFX_D(block, REG_A1, src_reg, 0, 16);
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
codegen_MEM_STORE_IMM_8(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);

    host_loong64_ADD_W_REG(block, REG_A0, seg_reg, addr_reg);
    host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 32);
    host_loong64_mov_imm(block, REG_A1, uop->imm_data);
    host_loong64_call(block, codegen_mem_store_byte);
    host_loong64_branch_reg_ne(block, REG_A1, REG_ZERO, codegen_exit_rout);

    return 0;
}
static int
codegen_MEM_STORE_IMM_16(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);

    host_loong64_ADD_W_REG(block, REG_A0, seg_reg, addr_reg);
    host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 32);
    host_loong64_mov_imm(block, REG_A1, uop->imm_data);
    host_loong64_call(block, codegen_mem_store_word);
    host_loong64_branch_reg_ne(block, REG_A1, REG_ZERO, codegen_exit_rout);

    return 0;
}
static int
codegen_MEM_STORE_IMM_32(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);

    host_loong64_ADD_W_REG(block, REG_A0, seg_reg, addr_reg);
    host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 32);
    host_loong64_mov_imm_w(block, REG_A1, (uint32_t) uop->imm_data);
    host_loong64_call(block, codegen_mem_store_long);
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
        host_loong64_UBFX_D(block, dest_reg, src_reg, 0, 16);
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

/*Shifts / rotates. The generic layer masks variable shift counts to 0x1f
  and skips the uop when the count is 0, so the .w shift forms' 5-bit count
  fields and x86's 5-bit count mask agree exactly.*/

static int
codegen_SHL(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_loong64_SHL_W_REG(block, dest_reg, src_reg, shift_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_loong64_SHL_W_REG(block, REG_TEMP, src_reg, shift_reg);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_loong64_SHL_W_REG(block, REG_TEMP, src_reg, shift_reg);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 8, 8);
        host_loong64_SHL_W_REG(block, REG_TEMP, REG_TEMP, shift_reg);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("SHL %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_SHL_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_loong64_SHL_W_IMM(block, dest_reg, src_reg, (int) uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_loong64_SHL_W_IMM(block, REG_TEMP, src_reg, (int) uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_loong64_SHL_W_IMM(block, REG_TEMP, src_reg, (int) uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 8, 8);
        host_loong64_SHL_W_IMM(block, REG_TEMP, REG_TEMP, (int) uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("SHL_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_SHR(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_loong64_SHR_W_REG(block, dest_reg, src_reg, shift_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        /*Mask first: garbage above the value's width must not leak down
          into the shifted low bits.*/
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 0, 16);
        host_loong64_SHR_W_REG(block, REG_TEMP, REG_TEMP, shift_reg);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_loong64_ANDI(block, REG_TEMP, src_reg, 0xff);
        host_loong64_SHR_W_REG(block, REG_TEMP, REG_TEMP, shift_reg);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 8, 8);
        host_loong64_SHR_W_REG(block, REG_TEMP, REG_TEMP, shift_reg);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("SHR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_SHR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_loong64_SHR_W_IMM(block, dest_reg, src_reg, (int) uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 0, 16);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, (int) uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_loong64_ANDI(block, REG_TEMP, src_reg, 0xff);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, (int) uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 8, 8);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, (int) uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("SHR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_SAR(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_loong64_SAR_W_REG(block, dest_reg, src_reg, shift_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_loong64_SEXT_H(block, REG_TEMP, src_reg);
        host_loong64_SAR_W_REG(block, REG_TEMP, REG_TEMP, shift_reg);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_loong64_SEXT_B(block, REG_TEMP, src_reg);
        host_loong64_SAR_W_REG(block, REG_TEMP, REG_TEMP, shift_reg);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_SEXT_H(block, REG_TEMP, src_reg);
        host_loong64_SAR_W_REG(block, REG_TEMP, REG_TEMP, shift_reg);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("SAR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_SAR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_loong64_SAR_W_IMM(block, dest_reg, src_reg, (int) uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_loong64_SEXT_H(block, REG_TEMP, src_reg);
        host_loong64_SAR_W_IMM(block, REG_TEMP, REG_TEMP, (int) uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_loong64_SEXT_B(block, REG_TEMP, src_reg);
        host_loong64_SAR_W_IMM(block, REG_TEMP, REG_TEMP, (int) uop->imm_data);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_SEXT_H(block, REG_TEMP, src_reg);
        host_loong64_SAR_W_IMM(block, REG_TEMP, REG_TEMP, (int) uop->imm_data);
        host_loong64_SHR_W_IMM(block, REG_TEMP, REG_TEMP, 8);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("SAR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}

/*rot = rotr of the duplicated field: rotr by (bits - count) == rol by
  count; for the 8/16-bit forms the value is duplicated so the rotate
  wraps within the field width. Variable counts are 1..31 (count 0 never
  reaches the uops - codegen_ops_shift.c exits to the interpreter).*/

static int
codegen_ROL(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        /*ROL32 == ROR32(32 - count); count is 1..31 so the rotate field
          is 1..31 too (count 0 never reaches the uops).*/
        host_loong64_mov_imm_w(block, REG_TEMP2, 32);
        host_loong64_SUB_W_REG(block, REG_TEMP2, REG_TEMP2, shift_reg);
        host_loong64_ROTR_W_REG(block, dest_reg, src_reg, REG_TEMP2);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        /*Count is masked to 0x1f upstream, so it can exceed 15: rotr.w's
          own 5-bit field makes (16 - count) & 31 the correct rotation for
          every count, with the duplicated copy providing the wrap bit.*/
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 0, 16);
        host_loong64_mov_imm_w(block, REG_TEMP2, 16);
        host_loong64_SUB_W_REG(block, REG_TEMP2, REG_TEMP2, shift_reg);
        host_loong64_SHL_D_IMM(block, REG_TEMP3, REG_TEMP, 16);
        host_loong64_OR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP3);
        host_loong64_ROTR_W_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_loong64_mov_imm_w(block, REG_TEMP2, 8);
        host_loong64_SUB_W_REG(block, REG_TEMP2, REG_TEMP2, shift_reg);
        host_loong64_ANDI(block, REG_TEMP2, REG_TEMP2, 7);
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 0, 8);
        host_loong64_SHL_D_IMM(block, REG_TEMP3, REG_TEMP, 8);
        host_loong64_OR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP3);
        host_loong64_SHR_D_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_mov_imm_w(block, REG_TEMP2, 8);
        host_loong64_SUB_W_REG(block, REG_TEMP2, REG_TEMP2, shift_reg);
        host_loong64_ANDI(block, REG_TEMP2, REG_TEMP2, 7);
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 8, 8);
        host_loong64_SHL_D_IMM(block, REG_TEMP3, REG_TEMP, 8);
        host_loong64_OR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP3);
        host_loong64_SHR_D_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("ROL %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_ROL_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (!(uop->imm_data & 31)) {
            if (src_reg != dest_reg)
                host_loong64_MOV_REG(block, dest_reg, src_reg);
        } else {
            host_loong64_ROTR_W_IMM(block, dest_reg, src_reg, 32 - (uop->imm_data & 31));
        }
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if ((uop->imm_data & 15) == 0) {
            if (src_reg != dest_reg)
                host_loong64_BFI_W(block, dest_reg, src_reg, 0, 16);
        } else {
            host_loong64_UBFX_D(block, REG_TEMP, src_reg, 0, 16);
            host_loong64_SHL_D_IMM(block, REG_TEMP3, REG_TEMP, 16);
            host_loong64_OR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP3);
            host_loong64_SHR_D_IMM(block, REG_TEMP, REG_TEMP, 16 - (uop->imm_data & 15));
            host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
        }
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if ((uop->imm_data & 7) == 0) {
            if (src_reg != dest_reg)
                host_loong64_BFI_W(block, dest_reg, src_reg, 0, 8);
        } else {
            host_loong64_UBFX_D(block, REG_TEMP, src_reg, 0, 8);
            host_loong64_SHL_D_IMM(block, REG_TEMP3, REG_TEMP, 8);
            host_loong64_OR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP3);
            host_loong64_SHR_D_IMM(block, REG_TEMP, REG_TEMP, 8 - (uop->imm_data & 7));
            host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
        }
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        if ((uop->imm_data & 7) == 0) {
            if (src_reg != dest_reg)
                fatal("ROL_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
        } else {
            host_loong64_UBFX_D(block, REG_TEMP, src_reg, 8, 8);
            host_loong64_SHL_D_IMM(block, REG_TEMP3, REG_TEMP, 8);
            host_loong64_OR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP3);
            host_loong64_SHR_D_IMM(block, REG_TEMP, REG_TEMP, 8 - (uop->imm_data & 7));
            host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
        }
    } else
        fatal("ROL_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_ROR(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_loong64_ROTR_W_REG(block, dest_reg, src_reg, shift_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 0, 16);
        host_loong64_ANDI(block, REG_TEMP2, shift_reg, 15);
        host_loong64_SHL_D_IMM(block, REG_TEMP3, REG_TEMP, 16);
        host_loong64_OR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP3);
        host_loong64_SHR_D_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 0, 8);
        host_loong64_ANDI(block, REG_TEMP2, shift_reg, 7);
        host_loong64_SHL_D_IMM(block, REG_TEMP3, REG_TEMP, 8);
        host_loong64_OR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP3);
        host_loong64_SHR_D_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        host_loong64_UBFX_D(block, REG_TEMP, src_reg, 8, 8);
        host_loong64_ANDI(block, REG_TEMP2, shift_reg, 7);
        host_loong64_SHL_D_IMM(block, REG_TEMP3, REG_TEMP, 8);
        host_loong64_OR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP3);
        host_loong64_SHR_D_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
    } else
        fatal("ROR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_ROR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (!(uop->imm_data & 31)) {
            if (src_reg != dest_reg)
                host_loong64_MOV_REG(block, dest_reg, src_reg);
        } else {
            host_loong64_ROTR_W_IMM(block, dest_reg, src_reg, (int) (uop->imm_data & 31));
        }
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if ((uop->imm_data & 15) == 0) {
            if (src_reg != dest_reg)
                fatal("ROR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
        } else {
            host_loong64_UBFX_D(block, REG_TEMP, src_reg, 0, 16);
            host_loong64_SHL_D_IMM(block, REG_TEMP3, REG_TEMP, 16);
            host_loong64_OR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP3);
            host_loong64_SHR_D_IMM(block, REG_TEMP, REG_TEMP, (int) (uop->imm_data & 15));
            host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
        }
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if ((uop->imm_data & 7) == 0) {
            if (src_reg != dest_reg)
                fatal("ROR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
        } else {
            host_loong64_UBFX_D(block, REG_TEMP, src_reg, 0, 8);
            host_loong64_SHL_D_IMM(block, REG_TEMP3, REG_TEMP, 8);
            host_loong64_OR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP3);
            host_loong64_SHR_D_IMM(block, REG_TEMP, REG_TEMP, (int) (uop->imm_data & 7));
            host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 8);
        }
    } else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size)) {
        if ((uop->imm_data & 7) == 0) {
            if (src_reg != dest_reg)
                fatal("ROR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
        } else {
            host_loong64_UBFX_D(block, REG_TEMP, src_reg, 8, 8);
            host_loong64_SHL_D_IMM(block, REG_TEMP3, REG_TEMP, 8);
            host_loong64_OR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP3);
            host_loong64_SHR_D_IMM(block, REG_TEMP, REG_TEMP, (int) (uop->imm_data & 7));
            host_loong64_BFI_W(block, dest_reg, REG_TEMP, 8, 8);
        }
    } else
        fatal("ROR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

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
        host_loong64_UBFX_D(block, REG_TEMP, src_reg_b, 0, 16);
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

    [UOP_CMP_IMM_JZ_DEST & UOP_MASK]    = codegen_CMP_IMM_JZ_DEST,
    [UOP_CMP_IMM_JNZ_DEST & UOP_MASK]   = codegen_CMP_IMM_JNZ_DEST,
    [UOP_CMP_JB & UOP_MASK]             = codegen_CMP_JB,
    [UOP_CMP_JNBE & UOP_MASK]           = codegen_CMP_JNBE,

    [UOP_CMP_JNB_DEST & UOP_MASK]  = codegen_CMP_JNB_DEST,
    [UOP_CMP_JNBE_DEST & UOP_MASK] = codegen_CMP_JNBE_DEST,
    [UOP_CMP_JNL_DEST & UOP_MASK]  = codegen_CMP_JNL_DEST,
    [UOP_CMP_JNLE_DEST & UOP_MASK] = codegen_CMP_JNLE_DEST,
    [UOP_CMP_JNO_DEST & UOP_MASK]  = codegen_CMP_JNO_DEST,
    [UOP_CMP_JNZ_DEST & UOP_MASK]  = codegen_CMP_JNZ_DEST,
    [UOP_CMP_JB_DEST & UOP_MASK]   = codegen_CMP_JB_DEST,
    [UOP_CMP_JBE_DEST & UOP_MASK]  = codegen_CMP_JBE_DEST,
    [UOP_CMP_JL_DEST & UOP_MASK]   = codegen_CMP_JL_DEST,
    [UOP_CMP_JLE_DEST & UOP_MASK]  = codegen_CMP_JLE_DEST,
    [UOP_CMP_JO_DEST & UOP_MASK]   = codegen_CMP_JO_DEST,
    [UOP_CMP_JZ_DEST & UOP_MASK]   = codegen_CMP_JZ_DEST,

    [UOP_TEST_JNS_DEST & UOP_MASK] = codegen_TEST_JNS_DEST,
    [UOP_TEST_JS_DEST & UOP_MASK]  = codegen_TEST_JS_DEST,

    [UOP_SAR & UOP_MASK]       = codegen_SAR,
    [UOP_SAR_IMM & UOP_MASK]   = codegen_SAR_IMM,
    [UOP_SHL & UOP_MASK]       = codegen_SHL,
    [UOP_SHL_IMM & UOP_MASK]   = codegen_SHL_IMM,
    [UOP_SHR & UOP_MASK]       = codegen_SHR,
    [UOP_SHR_IMM & UOP_MASK]   = codegen_SHR_IMM,
    [UOP_ROL & UOP_MASK]       = codegen_ROL,
    [UOP_ROL_IMM & UOP_MASK]   = codegen_ROL_IMM,
    [UOP_ROR & UOP_MASK]       = codegen_ROR,
    [UOP_ROR_IMM & UOP_MASK]   = codegen_ROR_IMM,

    [UOP_MEM_STORE_IMM_8 & UOP_MASK]  = codegen_MEM_STORE_IMM_8,
    [UOP_MEM_STORE_IMM_16 & UOP_MASK] = codegen_MEM_STORE_IMM_16,
    [UOP_MEM_STORE_IMM_32 & UOP_MASK] = codegen_MEM_STORE_IMM_32,

    [UOP_NOP_BARRIER & UOP_MASK] = codegen_NOP,

#ifdef DEBUG_EXTRA
    [UOP_LOG_INSTR & UOP_MASK] = codegen_LOG_INSTR,
#endif
};

#endif

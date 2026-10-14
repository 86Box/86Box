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
 *          uop handlers (M1: integer core + control flow; M3: x87/FPU;
 *          M4: MMX).
 *
 *          Handlers mirror the arm64 backend's semantics; every unsupported
 *          uop dispatches to a fatal() stub so gaps surface
 *          immediately on the target machine.
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
codegen_ANDN(codeblock_t *block, uop_t *uop)
{
    int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
    int src_a = HOST_REG_GET(uop->src_reg_a_real);
    int src_b = HOST_REG_GET(uop->src_reg_b_real);
    if (REG_IS_Q(IREG_GET_SIZE(uop->dest_reg_a_real)) &&
        REG_IS_Q(IREG_GET_SIZE(uop->src_reg_a_real)) &&
        REG_IS_Q(IREG_GET_SIZE(uop->src_reg_b_real))) {
        /* x86 PANDN is (~a) & b; LSX vandn.v is a & ~b. */
        host_loong64_LSX_3R(block, 0x71280000, dest_reg, src_b, src_a);
    } else
        fatal("ANDN %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
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

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b)) {
        host_loong64_LSX_3R(block, 0x71260000, dest_reg, src_reg_a, src_reg_b); /* vand.v */
    } else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
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
  branching.*/

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
    int width = REG_IS_L(size_a) ? 32 : (REG_IS_W(size_a) ? 16 : 8);

    cmp_sext_pair(block, size_a, size_b, src_a, src_b, &ra, &rb);
    host_loong64_SUBX_REG(block, REG_TEMP3, ra, rb);
    host_loong64_XOR_REG(block, rb, ra, rb);
    host_loong64_XOR_REG(block, ra, ra, REG_TEMP3);
    /*The algebra leaves the overflow sign at bit (width - 1) - the
      sign-extended inputs make the 64-bit difference exact, so its
      bit (width - 1) is the overflow of the width's arithmetic, but
      the branch templates test bit 63. Move the flag up first.*/
    host_loong64_AND_REG(block, REG_TEMP, ra, rb);
    if (width != 64)
        host_loong64_SHL_D_IMM(block, REG_TEMP, REG_TEMP, 64 - width);
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
    } else if (REG_IS_D(dest_size) && REG_IS_D(src_size)) {
        /*x87 ST(i) copy (FLD/FST/FXCH): 64-bit FP<->FP move.*/
        host_loong64_VMOV_F(block, dest_reg, src_reg);
    } else if (REG_IS_Q(dest_size) && REG_IS_Q(src_size)) {
        /*ST(i)_i64 mirror and Q temps live in the FP set as well.*/
        host_loong64_VMOV_F(block, dest_reg, src_reg);
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

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b)) {
        host_loong64_LSX_3R(block, 0x71268000, dest_reg, src_reg_a, src_reg_b); /* vor.v */
    } else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
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

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b)) {
        host_loong64_LSX_3R(block, 0x71270000, dest_reg, src_reg_a, src_reg_b); /* vxor.v */
    } else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
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

/*=== FP support (x87), mirroring the arm64 backend's semantics ===
  All D-class x87 values live in host FPRs (IREG_ST(r) with sizes
  resolved at compile time); Q-size integer temps are FP-class too.
  The x87 C0/C2/C3 condition bits are materialised into a W GPR via a
  quiet FCMP into FCC0-2 plus movcf2gr/sub.d sign-mask/slli/OR chains
  (LA64 has no CSEL-on-FCC and no fcc->GPR with bit placement). */

static int
codegen_FADD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        host_loong64_FADD_D(block, dest_reg, src_reg_a, src_reg_b);
    } else
        fatal("codegen_FADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    return 0;
}
static int
codegen_FSUB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        host_loong64_FSUB_D(block, dest_reg, src_reg_a, src_reg_b);
    } else
        fatal("codegen_FSUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    return 0;
}
static int
codegen_FMUL(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        host_loong64_FMUL_D(block, dest_reg, src_reg_a, src_reg_b);
    } else
        fatal("codegen_FMUL %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    return 0;
}
static int
codegen_FDIV(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        host_loong64_FDIV_D(block, dest_reg, src_reg_a, src_reg_b);
    } else
        fatal("codegen_FDIV %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    return 0;
}
static int
codegen_FABS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a)) {
        host_loong64_FABS_D(block, dest_reg, src_reg_a);
    } else
        fatal("codegen_FABS %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_FCHS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a)) {
        host_loong64_FNEG_D(block, dest_reg, src_reg_a);
    } else
        fatal("codegen_FCHS %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_FSQRT(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a)) {
        host_loong64_FSQRT_D(block, dest_reg, src_reg_a);
    } else
        fatal("codegen_FSQRT %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_FROUND_S(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a)) {
        /*double -> float -> double, host RM = nearest (the same
          approximation both references make for 24-bit precision).*/
        host_loong64_FCVT_S_D(block, REG_V_TEMP, src_reg_a);
        host_loong64_FCVT_D_S(block, dest_reg, REG_V_TEMP);
    } else
        fatal("codegen_FROUND_S %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}

/*Emit the x87 C0/C2/C3 nibble for a compare of src_a vs src_b into the
  W-size GPR dest:
    C3 (0x4000) = equal, C0 (0x0100) = less, C0|C2|C3 (0x4500) = unordered.
  Three quiet FCMPs into FCC0-2, then movcf2gr (whole-register,
  zero-extended 0/1 per the ld8u semantics) + slli/or to place the bits.
  No sign-mask trick is needed - the 0/1 values shift into exact flag
  bits, so the result register always holds a clean 32-bit value. LA64
  has no CSEL-on-FCC; C1 is never set, matching both reference backends
  and the interpreter (x87_compare).*/
static void
emit_fcom_flags(codeblock_t *block, int dest_reg, int src_reg_a, int src_reg_b)
{
    host_loong64_MOV_REG(block, dest_reg, REG_ZERO);
    host_loong64_FCMP_D(block, 0, src_reg_a, src_reg_b, 8); /*cun -> FCC0*/
    host_loong64_FCMP_D(block, 1, src_reg_a, src_reg_b, 4); /*ceq -> FCC1*/
    host_loong64_FCMP_D(block, 2, src_reg_a, src_reg_b, 2); /*clt -> FCC2*/

    /*unordered: C0|C2|C3 (the same 0/1 register shifted three times).*/
    host_loong64_MOVCF2GR(block, REG_TEMP, 0);
    host_loong64_SHL_D_IMM(block, REG_TEMP, REG_TEMP, 8); /*C0*/
    host_loong64_OR_REG(block, dest_reg, dest_reg, REG_TEMP);
    host_loong64_SHL_D_IMM(block, REG_TEMP, REG_TEMP, 2); /*C2*/
    host_loong64_OR_REG(block, dest_reg, dest_reg, REG_TEMP);
    host_loong64_SHL_D_IMM(block, REG_TEMP, REG_TEMP, 4); /*C3*/
    host_loong64_OR_REG(block, dest_reg, dest_reg, REG_TEMP);
    /*equal: C3.*/
    host_loong64_MOVCF2GR(block, REG_TEMP2, 1);
    host_loong64_SHL_D_IMM(block, REG_TEMP2, REG_TEMP2, 14); /*C3*/
    host_loong64_OR_REG(block, dest_reg, dest_reg, REG_TEMP2);
    /*less: C0.*/
    host_loong64_MOVCF2GR(block, REG_TEMP3, 2);
    host_loong64_SHL_D_IMM(block, REG_TEMP3, REG_TEMP3, 8); /*C0*/
    host_loong64_OR_REG(block, dest_reg, dest_reg, REG_TEMP3);
}
static int
codegen_FCOM(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_W(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        emit_fcom_flags(block, dest_reg, src_reg_a, src_reg_b);
    } else
        fatal("codegen_FCOM %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    return 0;
}
static int
codegen_FTST(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_W(dest_size) && REG_IS_D(src_size_a)) {
        host_loong64_MOVGR2FR_D(block, REG_V_TEMP, REG_ZERO); /*+0.0*/
        emit_fcom_flags(block, dest_reg, src_reg_a, REG_V_TEMP);
    } else
        fatal("codegen_FTST %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}

static int
codegen_MEM_LOAD_SINGLE(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (!REG_IS_D(dest_size))
        fatal("MEM_LOAD_SINGLE - %02x\n", uop->dest_reg_a_real);

    host_loong64_ADDX_REG(block, REG_A0, seg_reg, addr_reg);
    if (uop->imm_data)
        host_loong64_ADD_W_IMM(block, REG_A0, REG_A0, (uint32_t) uop->imm_data);
    host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 32);
    host_loong64_call(block, codegen_mem_load_single);
    host_loong64_branch_reg_ne(block, REG_A1, REG_ZERO, codegen_exit_rout);
    /*The stub returns the raw f32 bits in V_TEMP; convert here like the
      arm64 backend does.*/
    host_loong64_FCVT_D_S(block, dest_reg, REG_V_TEMP);

    return 0;
}
static int
codegen_MEM_LOAD_DOUBLE(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (!REG_IS_D(dest_size))
        fatal("MEM_LOAD_DOUBLE - %02x\n", uop->dest_reg_a_real);

    host_loong64_ADDX_REG(block, REG_A0, seg_reg, addr_reg);
    if (uop->imm_data)
        host_loong64_ADD_W_IMM(block, REG_A0, REG_A0, (uint32_t) uop->imm_data);
    host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 32);
    host_loong64_call(block, codegen_mem_load_double);
    host_loong64_branch_reg_ne(block, REG_A1, REG_ZERO, codegen_exit_rout);
    host_loong64_VMOV_F(block, dest_reg, REG_V_TEMP);

    return 0;
}
static int
codegen_MEM_STORE_SINGLE(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);
    int src_reg  = HOST_REG_GET(uop->src_reg_c_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_c_real);

    if (!REG_IS_D(src_size))
        fatal("MEM_STORE_SINGLE - %02x\n", uop->dest_reg_a_real);

    host_loong64_ADDX_REG(block, REG_A0, seg_reg, addr_reg);
    if (uop->imm_data)
        host_loong64_ADD_W_IMM(block, REG_A0, REG_A0, (uint32_t) uop->imm_data);
    host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 32);
    host_loong64_FCVT_S_D(block, REG_V_TEMP, src_reg);
    host_loong64_call(block, codegen_mem_store_single);
    host_loong64_branch_reg_ne(block, REG_A1, REG_ZERO, codegen_exit_rout);

    return 0;
}
static int
codegen_MEM_STORE_DOUBLE(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);
    int src_reg  = HOST_REG_GET(uop->src_reg_c_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_c_real);

    if (!REG_IS_D(src_size))
        fatal("MEM_STORE_DOUBLE - %02x\n", uop->dest_reg_a_real);

    host_loong64_ADDX_REG(block, REG_A0, seg_reg, addr_reg);
    if (uop->imm_data)
        host_loong64_ADD_W_IMM(block, REG_A0, REG_A0, (uint32_t) uop->imm_data);
    host_loong64_UBFX_D(block, REG_A0, REG_A0, 0, 32);
    host_loong64_VMOV_F(block, REG_V_TEMP, src_reg);
    host_loong64_call(block, codegen_mem_store_double);
    host_loong64_branch_reg_ne(block, REG_A1, REG_ZERO, codegen_exit_rout);

    return 0;
}

static int
codegen_MOV_DOUBLE_INT(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_L(src_size)) {
        host_loong64_MOVGR2FR_W(block, REG_V_TEMP, src_reg);
        host_loong64_FFINT_D_W(block, dest_reg, REG_V_TEMP);
    } else if (REG_IS_D(dest_size) && REG_IS_W(src_size)) {
        host_loong64_SEXT_H(block, REG_TEMP, src_reg);
        host_loong64_MOVGR2FR_W(block, REG_V_TEMP, REG_TEMP);
        host_loong64_FFINT_D_W(block, dest_reg, REG_V_TEMP);
    } else if (REG_IS_D(dest_size) && REG_IS_Q(src_size)) {
        /*Q-size integer regs are FP-class (they hold raw int64 in an
          FPR), so ffint.d.l converts directly.*/
        host_loong64_FFINT_D_L(block, dest_reg, src_reg);
    } else
        fatal("codegen_MOV_DOUBLE_INT %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_MOV_INT_DOUBLE(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_D(src_size)) {
        host_loong64_VMOV_F(block, REG_V_TEMP, src_reg);
        host_loong64_call(block, codegen_fp_round);
        host_loong64_MOVFR2GR_S(block, dest_reg, REG_V_TEMP);
    } else if (REG_IS_W(dest_size) && REG_IS_D(src_size)) {
        host_loong64_VMOV_F(block, REG_V_TEMP, src_reg);
        host_loong64_call(block, codegen_fp_round);
        host_loong64_MOVFR2GR_S(block, REG_TEMP, REG_V_TEMP);
        host_loong64_BFI_W(block, dest_reg, REG_TEMP, 0, 16);
    } else
        fatal("codegen_MOV_INT_DOUBLE %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    return 0;
}
static int
codegen_MOV_INT_DOUBLE_64(codeblock_t *block, uop_t *uop)
{
    int dest_reg    = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg     = HOST_REG_GET(uop->src_reg_a_real);
    int src_64_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int tag_reg     = HOST_REG_GET(uop->src_reg_c_real);
    int dest_size   = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size    = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_64_size = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_D(src_size) && REG_IS_Q(src_64_size)) {
        uint32_t *branch_offset;

        /*If TAG_UINT64 is set then the source is MM[] (raw int64 bits in
          an FPR). Otherwise it is a double in ST() and needs rounding to
          64-bit integer bits via the fp_round_quad stub. Q-size IR regs
          are FP-class, so both halves move through FPRs only.*/
        host_loong64_VMOV_F(block, dest_reg, src_64_reg);
        host_loong64_ANDI(block, REG_TEMP, tag_reg, TAG_UINT64);
        branch_offset = host_loong64_BNE_(block, REG_TEMP, REG_ZERO);

        host_loong64_VMOV_F(block, REG_V_TEMP, src_reg);
        host_loong64_call(block, codegen_fp_round_quad);
        host_loong64_VMOV_F(block, dest_reg, REG_V_TEMP);

        host_loong64_branch_set_offset(branch_offset, &block_write_data[block_pos]);
    } else
        fatal("codegen_MOV_INT_DOUBLE_64 %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    return 0;
}

/* MMX values live in the low 64 bits of an LSX register.  Most packed
   operations therefore map one-for-one to LSX; the upper lanes are dead. */
static int
codegen_MMX_BINOP(codeblock_t *block, uop_t *uop)
{
    int d = HOST_REG_GET(uop->dest_reg_a_real);
    int a = HOST_REG_GET(uop->src_reg_a_real);
    int b = HOST_REG_GET(uop->src_reg_b_real);
    uint32_t op;

    if (!REG_IS_Q(IREG_GET_SIZE(uop->dest_reg_a_real)) ||
        !REG_IS_Q(IREG_GET_SIZE(uop->src_reg_a_real)) ||
        !REG_IS_Q(IREG_GET_SIZE(uop->src_reg_b_real)))
        fatal("MMX_BINOP %08x %02x %02x %02x\n", uop->type, uop->dest_reg_a_real,
              uop->src_reg_a_real, uop->src_reg_b_real);

    switch (uop->type & UOP_MASK) {
        case UOP_PADDB & UOP_MASK:   op = 0x700a0000; break;
        case UOP_PADDW & UOP_MASK:   op = 0x700a8000; break;
        case UOP_PADDD & UOP_MASK:   op = 0x700b0000; break;
        case UOP_PADDSB & UOP_MASK:  op = 0x70460000; break;
        case UOP_PADDSW & UOP_MASK:  op = 0x70468000; break;
        case UOP_PADDUSB & UOP_MASK: op = 0x704a0000; break;
        case UOP_PADDUSW & UOP_MASK: op = 0x704a8000; break;
        case UOP_PSUBB & UOP_MASK:   op = 0x700c0000; break;
        case UOP_PSUBW & UOP_MASK:   op = 0x700c8000; break;
        case UOP_PSUBD & UOP_MASK:   op = 0x700d0000; break;
        case UOP_PSUBSB & UOP_MASK:  op = 0x70480000; break;
        case UOP_PSUBSW & UOP_MASK:  op = 0x70488000; break;
        case UOP_PSUBUSB & UOP_MASK: op = 0x704c0000; break;
        case UOP_PSUBUSW & UOP_MASK: op = 0x704c8000; break;
        case UOP_PCMPEQB & UOP_MASK: op = 0x70000000; break;
        case UOP_PCMPEQW & UOP_MASK: op = 0x70008000; break;
        case UOP_PCMPEQD & UOP_MASK: op = 0x70010000; break;
        case UOP_PCMPGTB & UOP_MASK: op = 0x70060000; { int t = a; a = b; b = t; } break;
        case UOP_PCMPGTW & UOP_MASK: op = 0x70068000; { int t = a; a = b; b = t; } break;
        case UOP_PCMPGTD & UOP_MASK: op = 0x70070000; { int t = a; a = b; b = t; } break;
        case UOP_PMULLW & UOP_MASK:  op = 0x70848000; break;
        case UOP_PMULHW & UOP_MASK:  op = 0x70868000; break;
        default: fatal("MMX_BINOP unknown %08x\n", uop->type); return 0;
    }
    host_loong64_LSX_3R(block, op, d, a, b);
    return 0;
}

static int
codegen_MMX_SHIFT(codeblock_t *block, uop_t *uop)
{
    int d = HOST_REG_GET(uop->dest_reg_a_real);
    int s = HOST_REG_GET(uop->src_reg_a_real);
    unsigned count = (unsigned) uop->imm_data;
    unsigned width;
    uint32_t op;
    int arithmetic = 0;

    if (!REG_IS_Q(IREG_GET_SIZE(uop->dest_reg_a_real)) ||
        !REG_IS_Q(IREG_GET_SIZE(uop->src_reg_a_real)))
        fatal("MMX_SHIFT %08x %02x %02x\n", uop->type, uop->dest_reg_a_real, uop->src_reg_a_real);

    switch (uop->type & UOP_MASK) {
        case UOP_PSLLW_IMM & UOP_MASK: width = 16; op = 0x732c4000; break;
        case UOP_PSLLD_IMM & UOP_MASK: width = 32; op = 0x732c8000; break;
        case UOP_PSLLQ_IMM & UOP_MASK: width = 64; op = 0x732d0000; break;
        case UOP_PSRLW_IMM & UOP_MASK: width = 16; op = 0x73304000; break;
        case UOP_PSRLD_IMM & UOP_MASK: width = 32; op = 0x73308000; break;
        case UOP_PSRLQ_IMM & UOP_MASK: width = 64; op = 0x73310000; break;
        case UOP_PSRAW_IMM & UOP_MASK: width = 16; op = 0x73344000; arithmetic = 1; break;
        case UOP_PSRAD_IMM & UOP_MASK: width = 32; op = 0x73348000; arithmetic = 1; break;
        case UOP_PSRAQ_IMM & UOP_MASK: width = 64; op = 0x73350000; arithmetic = 1; break;
        default: fatal("MMX_SHIFT unknown %08x\n", uop->type); return 0;
    }
    if (!count)
        host_loong64_VMOV_F(block, d, s);
    else if (count >= width && !arithmetic)
        host_loong64_LSX_3R(block, 0x71270000, d, d, d); /* vxor.v */
    else {
        if (count >= width)
            count = width - 1;
        host_loong64_LSX_2RI(block, op, d, s, count);
    }
    return 0;
}

static int
codegen_MMX_UNPACK(codeblock_t *block, uop_t *uop)
{
    int d = HOST_REG_GET(uop->dest_reg_a_real);
    int a = HOST_REG_GET(uop->src_reg_a_real);
    int b = HOST_REG_GET(uop->src_reg_b_real);
    uint32_t op;
    int high = 0;

    if (!REG_IS_Q(IREG_GET_SIZE(uop->dest_reg_a_real)) ||
        !REG_IS_Q(IREG_GET_SIZE(uop->src_reg_a_real)) ||
        !REG_IS_Q(IREG_GET_SIZE(uop->src_reg_b_real)))
        fatal("MMX_UNPACK %08x\n", uop->type);
    switch (uop->type & UOP_MASK) {
        case UOP_PUNPCKLBW & UOP_MASK: op = 0x711a0000; break;
        case UOP_PUNPCKLWD & UOP_MASK: op = 0x711a8000; break;
        case UOP_PUNPCKLDQ & UOP_MASK: op = 0x711b0000; break;
        case UOP_PUNPCKHBW & UOP_MASK: op = 0x711a0000; high = 1; break;
        case UOP_PUNPCKHWD & UOP_MASK: op = 0x711a8000; high = 1; break;
        case UOP_PUNPCKHDQ & UOP_MASK: op = 0x711b0000; high = 1; break;
        default: fatal("MMX_UNPACK unknown %08x\n", uop->type); return 0;
    }
    if (high) {
        /* Shift before overwriting d: d is normally also a. */
        host_loong64_LSX_2RI(block, 0x728e8000, REG_V_TEMP, a, 4);
        host_loong64_LSX_2RI(block, 0x728e8000, d, b, 4);
        host_loong64_LSX_3R(block, op, d, d, REG_V_TEMP);
    } else
        /* vilvl emits lanes from vk first, which is x86's a,b order. */
        host_loong64_LSX_3R(block, op, d, b, a);
    return 0;
}

static uint64_t
mmx_packsswb(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    for (int i = 0; i < 8; i++) {
        int16_t v = (int16_t) ((i < 4 ? a : b) >> ((i & 3) * 16));
        int n = v < -128 ? -128 : (v > 127 ? 127 : v);
        r |= (uint64_t) (uint8_t) n << (i * 8);
    }
    return r;
}

static uint64_t
mmx_packssdw(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    for (int i = 0; i < 4; i++) {
        int32_t v = (int32_t) ((i < 2 ? a : b) >> ((i & 1) * 32));
        int32_t n = v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
        r |= (uint64_t) (uint16_t) n << (i * 16);
    }
    return r;
}

static uint64_t
mmx_packuswb(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    for (int i = 0; i < 8; i++) {
        int16_t v = (int16_t) ((i < 4 ? a : b) >> ((i & 3) * 16));
        int n = v < 0 ? 0 : (v > 255 ? 255 : v);
        r |= (uint64_t) n << (i * 8);
    }
    return r;
}

static uint64_t
mmx_pmaddwd(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    for (int pair = 0; pair < 2; pair++) {
        int i = pair * 2;
        int32_t p0 = (int16_t) (a >> (i * 16)) * (int16_t) (b >> (i * 16));
        int32_t p1 = (int16_t) (a >> ((i + 1) * 16)) * (int16_t) (b >> ((i + 1) * 16));
        r |= (uint64_t) (uint32_t) ((uint32_t) p0 + (uint32_t) p1) << (pair * 32);
    }
    return r;
}

static int
codegen_MMX_HELPER2(codeblock_t *block, uop_t *uop)
{
    int d = HOST_REG_GET(uop->dest_reg_a_real);
    int a = HOST_REG_GET(uop->src_reg_a_real);
    int b = HOST_REG_GET(uop->src_reg_b_real);
    void *fn;
    if (!REG_IS_Q(IREG_GET_SIZE(uop->dest_reg_a_real)) ||
        !REG_IS_Q(IREG_GET_SIZE(uop->src_reg_a_real)) ||
        !REG_IS_Q(IREG_GET_SIZE(uop->src_reg_b_real)))
        fatal("MMX_HELPER2 %08x\n", uop->type);
    switch (uop->type & UOP_MASK) {
        case UOP_PACKSSWB & UOP_MASK: fn = mmx_packsswb; break;
        case UOP_PACKSSDW & UOP_MASK: fn = mmx_packssdw; break;
        case UOP_PACKUSWB & UOP_MASK: fn = mmx_packuswb; break;
        case UOP_PMADDWD & UOP_MASK:  fn = mmx_pmaddwd; break;
        default: fatal("MMX_HELPER2 unknown %08x\n", uop->type); return 0;
    }
    host_loong64_MOVFR2GR_D(block, REG_A0, a);
    host_loong64_MOVFR2GR_D(block, REG_A1, b);
    host_loong64_call(block, fn);
    host_loong64_MOVGR2FR_D(block, d, REG_A0);
    return 0;
}

const uOpFn uop_handlers[UOP_MAX] = {
    /*Any uop without a handler below lands here and fatal()s in both
      debug and release builds.*/
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
    [UOP_ANDN & UOP_MASK]      = codegen_ANDN,
    [UOP_AND_IMM & UOP_MASK]   = codegen_AND_IMM,
    [UOP_OR & UOP_MASK]        = codegen_OR,
    [UOP_OR_IMM & UOP_MASK]    = codegen_OR_IMM,
    [UOP_SUB & UOP_MASK]       = codegen_SUB,
    [UOP_SUB_IMM & UOP_MASK]   = codegen_SUB_IMM,
    [UOP_XOR & UOP_MASK]       = codegen_XOR,
    [UOP_XOR_IMM & UOP_MASK]   = codegen_XOR_IMM,

    [UOP_CMP_IMM_JZ & UOP_MASK] = codegen_CMP_IMM_JZ,

    /*FP (x87) support. UOP_FP_ENTER is not dispatched here - the
      uop_FP_ENTER macro takes the CALL codegen_fp_enter + CMP_IMM_JZ
      path on LoongArch64 (codegen_ir_defs.h), like arm64.*/
    [UOP_FADD & UOP_MASK]  = codegen_FADD,
    [UOP_FSUB & UOP_MASK]  = codegen_FSUB,
    [UOP_FMUL & UOP_MASK]  = codegen_FMUL,
    [UOP_FDIV & UOP_MASK]  = codegen_FDIV,
    [UOP_FCOM & UOP_MASK]  = codegen_FCOM,
    [UOP_FABS & UOP_MASK]  = codegen_FABS,
    [UOP_FCHS & UOP_MASK]  = codegen_FCHS,
    [UOP_FTST & UOP_MASK]  = codegen_FTST,
    [UOP_FSQRT & UOP_MASK] = codegen_FSQRT,
    [UOP_FROUND_S & UOP_MASK] = codegen_FROUND_S,

    [UOP_PADDB & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PADDW & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PADDD & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PADDSB & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PADDSW & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PADDUSB & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PADDUSW & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PSUBB & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PSUBW & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PSUBD & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PSUBSB & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PSUBSW & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PSUBUSB & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PSUBUSW & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PCMPEQB & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PCMPEQW & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PCMPEQD & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PCMPGTB & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PCMPGTW & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PCMPGTD & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PMULLW & UOP_MASK] = codegen_MMX_BINOP,
    [UOP_PMULHW & UOP_MASK] = codegen_MMX_BINOP,

    [UOP_PSLLW_IMM & UOP_MASK] = codegen_MMX_SHIFT,
    [UOP_PSLLD_IMM & UOP_MASK] = codegen_MMX_SHIFT,
    [UOP_PSLLQ_IMM & UOP_MASK] = codegen_MMX_SHIFT,
    [UOP_PSRAW_IMM & UOP_MASK] = codegen_MMX_SHIFT,
    [UOP_PSRAD_IMM & UOP_MASK] = codegen_MMX_SHIFT,
    [UOP_PSRAQ_IMM & UOP_MASK] = codegen_MMX_SHIFT,
    [UOP_PSRLW_IMM & UOP_MASK] = codegen_MMX_SHIFT,
    [UOP_PSRLD_IMM & UOP_MASK] = codegen_MMX_SHIFT,
    [UOP_PSRLQ_IMM & UOP_MASK] = codegen_MMX_SHIFT,

    [UOP_PUNPCKLBW & UOP_MASK] = codegen_MMX_UNPACK,
    [UOP_PUNPCKLWD & UOP_MASK] = codegen_MMX_UNPACK,
    [UOP_PUNPCKLDQ & UOP_MASK] = codegen_MMX_UNPACK,
    [UOP_PUNPCKHBW & UOP_MASK] = codegen_MMX_UNPACK,
    [UOP_PUNPCKHWD & UOP_MASK] = codegen_MMX_UNPACK,
    [UOP_PUNPCKHDQ & UOP_MASK] = codegen_MMX_UNPACK,

    [UOP_PACKSSWB & UOP_MASK] = codegen_MMX_HELPER2,
    [UOP_PACKSSDW & UOP_MASK] = codegen_MMX_HELPER2,
    [UOP_PACKUSWB & UOP_MASK] = codegen_MMX_HELPER2,
    [UOP_PMADDWD & UOP_MASK] = codegen_MMX_HELPER2,

    [UOP_MEM_LOAD_SINGLE & UOP_MASK] = codegen_MEM_LOAD_SINGLE,
    [UOP_MEM_LOAD_DOUBLE & UOP_MASK] = codegen_MEM_LOAD_DOUBLE,
    [UOP_MEM_STORE_SINGLE & UOP_MASK] = codegen_MEM_STORE_SINGLE,
    [UOP_MEM_STORE_DOUBLE & UOP_MASK] = codegen_MEM_STORE_DOUBLE,

    [UOP_MOV_DOUBLE_INT & UOP_MASK]    = codegen_MOV_DOUBLE_INT,
    [UOP_MOV_INT_DOUBLE & UOP_MASK]    = codegen_MOV_INT_DOUBLE,
    [UOP_MOV_INT_DOUBLE_64 & UOP_MASK] = codegen_MOV_INT_DOUBLE_64,

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

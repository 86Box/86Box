#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"
#include "x86_flags.h"
#include "codegen.h"
#include "codegen_backend.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_jit_wrappers.h"
#include "codegen_ops_setcc.h"

static JIT_WRAPPER int
O_SET_01(void)
{
    return VF_SET() ? 1 : 0;
}
static JIT_WRAPPER int
B_SET_01(void)
{
    return CF_SET() ? 1 : 0;
}
static JIT_WRAPPER int
E_SET_01(void)
{
    return ZF_SET() ? 1 : 0;
}
static JIT_WRAPPER int
BE_SET_01(void)
{
    return (CF_SET() || ZF_SET()) ? 1 : 0;
}
static JIT_WRAPPER int
S_SET_01(void)
{
    return NF_SET() ? 1 : 0;
}
static JIT_WRAPPER int
P_SET_01(void)
{
    return PF_SET() ? 1 : 0;
}
static JIT_WRAPPER int
L_SET_01(void)
{
    return ((NF_SET() ? 1 : 0) != (VF_SET() ? 1 : 0)) ? 1 : 0;
}
static JIT_WRAPPER int
LE_SET_01(void)
{
    return (((NF_SET() ? 1 : 0) != (VF_SET() ? 1 : 0)) || ZF_SET()) ? 1 : 0;
}

/*The condition generators below leave 0 or 1 in IREG_temp0. They must be
  straight-line code - the register allocator's version tracking can't merge
  register writes on converging paths, so no conditional jumps are allowed.*/

static void
setcc_gen_O(ir_data_t *ir, int invert)
{
    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ZN16:
        case FLAGS_ZN32:
            /*Overflow is always zero*/
            uop_MOV_IMM(ir, IREG_temp0, invert ? 1 : 0);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT(ir, IREG_temp0, O_SET_01);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;
    }
}

static void
setcc_gen_B(ir_data_t *ir, int invert)
{
    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ZN16:
        case FLAGS_ZN32:
            /*Carry is always zero*/
            uop_MOV_IMM(ir, IREG_temp0, invert ? 1 : 0);
            break;

        case FLAGS_SUB8:
            uop_MOVZX(ir, IREG_temp0, IREG_flags_op1_B);
            uop_MOVZX(ir, IREG_temp1, IREG_flags_op2_B);
            uop_SUB(ir, IREG_temp0, IREG_temp0, IREG_temp1);
            uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 31); /*temp0 = (op1 < op2)*/
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;

        case FLAGS_SUB16:
            uop_MOVZX(ir, IREG_temp0, IREG_flags_op1_W);
            uop_MOVZX(ir, IREG_temp1, IREG_flags_op2_W);
            uop_SUB(ir, IREG_temp0, IREG_temp0, IREG_temp1);
            uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 31);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT(ir, IREG_temp0, B_SET_01);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;
    }
}

static void
setcc_gen_E(ir_data_t *ir, int invert)
{
    if (codegen_flags_changed && flags_res_valid()) {
        uop_MOV_IMM(ir, IREG_temp0, 0);
        uop_SUB(ir, IREG_temp0, IREG_temp0, IREG_flags_res);
        uop_OR(ir, IREG_temp0, IREG_temp0, IREG_flags_res);
        uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 31); /*temp0 = (flags_res != 0)*/
        if (!invert)
            uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
    } else {
        uop_CALL_FUNC_RESULT(ir, IREG_temp0, E_SET_01);
        if (invert)
            uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
    }
}

static void
setcc_gen_BE(ir_data_t *ir, int invert)
{
    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ZN16:
        case FLAGS_ZN32:
            /*Carry is always zero, so test zero only*/
            setcc_gen_E(ir, invert);
            break;

        case FLAGS_SUB8:
            uop_MOVZX(ir, IREG_temp0, IREG_flags_op2_B);
            uop_MOVZX(ir, IREG_temp1, IREG_flags_op1_B);
            uop_SUB(ir, IREG_temp0, IREG_temp0, IREG_temp1);
            uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 31); /*temp0 = (op2 < op1) = !BE*/
            if (!invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;

        case FLAGS_SUB16:
            uop_MOVZX(ir, IREG_temp0, IREG_flags_op2_W);
            uop_MOVZX(ir, IREG_temp1, IREG_flags_op1_W);
            uop_SUB(ir, IREG_temp0, IREG_temp0, IREG_temp1);
            uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 31);
            if (!invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT(ir, IREG_temp0, BE_SET_01);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;
    }
}

static void
setcc_gen_S(ir_data_t *ir, int invert)
{
    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ADD8:
        case FLAGS_SUB8:
        case FLAGS_SHL8:
        case FLAGS_SHR8:
        case FLAGS_SAR8:
        case FLAGS_INC8:
        case FLAGS_DEC8:
            uop_MOVZX(ir, IREG_temp0, IREG_flags_res_B);
            uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 7);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;

        case FLAGS_ZN16:
        case FLAGS_ADD16:
        case FLAGS_SUB16:
        case FLAGS_SHL16:
        case FLAGS_SHR16:
        case FLAGS_SAR16:
        case FLAGS_INC16:
        case FLAGS_DEC16:
            uop_MOVZX(ir, IREG_temp0, IREG_flags_res_W);
            uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 15);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;

        case FLAGS_ZN32:
        case FLAGS_ADD32:
        case FLAGS_SUB32:
        case FLAGS_SHL32:
        case FLAGS_SHR32:
        case FLAGS_SAR32:
        case FLAGS_INC32:
        case FLAGS_DEC32:
            uop_SHR_IMM(ir, IREG_temp0, IREG_flags_res, 31);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT(ir, IREG_temp0, S_SET_01);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;
    }
}

static void
setcc_gen_P(ir_data_t *ir, int invert)
{
    uop_CALL_FUNC_RESULT(ir, IREG_temp0, P_SET_01);
    if (invert)
        uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
}

static void
setcc_gen_L(ir_data_t *ir, int invert)
{
    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
            /*V flag is always clear. Condition is true if N is set*/
            uop_MOVZX(ir, IREG_temp0, IREG_flags_res_B);
            uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 7);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;
        case FLAGS_ZN16:
            uop_MOVZX(ir, IREG_temp0, IREG_flags_res_W);
            uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 15);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;
        case FLAGS_ZN32:
            uop_SHR_IMM(ir, IREG_temp0, IREG_flags_res, 31);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;

        case FLAGS_SUB8:
        case FLAGS_DEC8:
            uop_MOVSX(ir, IREG_temp0, IREG_flags_op1_B);
            uop_MOVSX(ir, IREG_temp1, IREG_flags_op2_B);
            uop_SUB(ir, IREG_temp0, IREG_temp0, IREG_temp1);
            uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 31); /*temp0 = (signed)(op1 < op2)*/
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;

        case FLAGS_SUB16:
        case FLAGS_DEC16:
            uop_MOVSX(ir, IREG_temp0, IREG_flags_op1_W);
            uop_MOVSX(ir, IREG_temp1, IREG_flags_op2_W);
            uop_SUB(ir, IREG_temp0, IREG_temp0, IREG_temp1);
            uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 31);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT(ir, IREG_temp0, L_SET_01);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;
    }
}

static void
setcc_gen_LE(ir_data_t *ir, int invert)
{
    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_SUB8:
        case FLAGS_DEC8:
            uop_MOVSX(ir, IREG_temp0, IREG_flags_op2_B);
            uop_MOVSX(ir, IREG_temp1, IREG_flags_op1_B);
            uop_SUB(ir, IREG_temp0, IREG_temp0, IREG_temp1);
            uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 31); /*temp0 = (signed)(op2 < op1) = !LE*/
            if (!invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;

        case FLAGS_SUB16:
        case FLAGS_DEC16:
            uop_MOVSX(ir, IREG_temp0, IREG_flags_op2_W);
            uop_MOVSX(ir, IREG_temp1, IREG_flags_op1_W);
            uop_SUB(ir, IREG_temp0, IREG_temp0, IREG_temp1);
            uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 31);
            if (!invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT(ir, IREG_temp0, LE_SET_01);
            if (invert)
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1);
            break;
    }
}

static uint32_t
ropSETCC_common(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc,
                void (*gen_cond)(ir_data_t *ir, int invert), int invert)
{
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        gen_cond(ir, invert);
        uop_MOV(ir, IREG_8(fetchdat & 7), IREG_temp0_B);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        /*The condition must be evaluated after the EA calculation -
          codegen_generate_ea can clobber IREG_temp0*/
        gen_cond(ir, invert);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
    }

    return op_pc + 1;
}

static void
cmov_select(ir_data_t *ir, int dest_reg, int src_reg)
{
    int size        = IREG_GET_SIZE(dest_reg);
    int mask_reg    = (size == IREG_SIZE_W) ? IREG_temp0_W : IREG_temp0;
    int scratch_reg = (size == IREG_SIZE_W) ? IREG_temp1_W : IREG_temp1;

    /* IREG_temp0 holds 0/1 from the condition generator. Turn it into
       0/all-ones and use dest ^= (dest ^ src) & mask. */
    uop_MOV_IMM(ir, scratch_reg, 0);
    uop_SUB(ir, mask_reg, scratch_reg, mask_reg);
    uop_MOV(ir, scratch_reg, dest_reg);
    uop_XOR(ir, scratch_reg, scratch_reg, src_reg);
    uop_AND(ir, scratch_reg, scratch_reg, mask_reg);
    uop_XOR(ir, dest_reg, dest_reg, scratch_reg);
}

static uint32_t
ropCMOV_common(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc,
               void (*gen_cond)(ir_data_t *ir, int invert), int invert, int is_32)
{
    int dest_reg;
    int src_reg;

    if ((fetchdat & 0xc0) != 0xc0)
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);

    if (is_32) {
        dest_reg = IREG_32((fetchdat >> 3) & 7);
        src_reg  = IREG_32(fetchdat & 7);
    } else {
        dest_reg = IREG_16((fetchdat >> 3) & 7);
        src_reg  = IREG_16(fetchdat & 7);
    }

    gen_cond(ir, invert);
    cmov_select(ir, dest_reg, src_reg);

    return op_pc + 1;
}

// clang-format off
#define ropSET(cond, gen, invert)                                       \
    uint32_t ropSET##cond(codeblock_t *block,                           \
                          ir_data_t *ir,                                \
                          UNUSED(uint8_t opcode),                       \
                          uint32_t fetchdat,                            \
                          uint32_t op_32,                               \
                          uint32_t op_pc)                               \
    {                                                                   \
        return ropSETCC_common(block, ir, fetchdat, op_32, op_pc, gen, invert); \
    }

ropSET(O,   setcc_gen_O,  0)
ropSET(NO,  setcc_gen_O,  1)
ropSET(B,   setcc_gen_B,  0)
ropSET(NB,  setcc_gen_B,  1)
ropSET(E,   setcc_gen_E,  0)
ropSET(NE,  setcc_gen_E,  1)
ropSET(BE,  setcc_gen_BE, 0)
ropSET(NBE, setcc_gen_BE, 1)
ropSET(S,   setcc_gen_S,  0)
ropSET(NS,  setcc_gen_S,  1)
ropSET(P,   setcc_gen_P,  0)
ropSET(NP,  setcc_gen_P,  1)
ropSET(L,   setcc_gen_L,  0)
ropSET(NL,  setcc_gen_L,  1)
ropSET(LE,  setcc_gen_LE, 0)
ropSET(NLE, setcc_gen_LE, 1)

#define ropCMOV(cond, gen, invert)                                      \
    uint32_t ropCMOV##cond##_w(codeblock_t *block,                      \
                               ir_data_t *ir,                           \
                               UNUSED(uint8_t opcode),                  \
                               uint32_t fetchdat,                       \
                               UNUSED(uint32_t op_32),                  \
                               uint32_t op_pc)                          \
    {                                                                   \
        return ropCMOV_common(block, ir, fetchdat, op_pc, gen, invert, 0); \
    }                                                                   \
                                                                        \
    uint32_t ropCMOV##cond##_l(codeblock_t *block,                      \
                               ir_data_t *ir,                           \
                               UNUSED(uint8_t opcode),                  \
                               uint32_t fetchdat,                       \
                               UNUSED(uint32_t op_32),                  \
                               uint32_t op_pc)                          \
    {                                                                   \
        return ropCMOV_common(block, ir, fetchdat, op_pc, gen, invert, 1); \
    }

ropCMOV(O,   setcc_gen_O,  0)
ropCMOV(NO,  setcc_gen_O,  1)
ropCMOV(B,   setcc_gen_B,  0)
ropCMOV(NB,  setcc_gen_B,  1)
ropCMOV(E,   setcc_gen_E,  0)
ropCMOV(NE,  setcc_gen_E,  1)
ropCMOV(BE,  setcc_gen_BE, 0)
ropCMOV(NBE, setcc_gen_BE, 1)
ropCMOV(S,   setcc_gen_S,  0)
ropCMOV(NS,  setcc_gen_S,  1)
ropCMOV(P,   setcc_gen_P,  0)
ropCMOV(NP,  setcc_gen_P,  1)
ropCMOV(L,   setcc_gen_L,  0)
ropCMOV(NL,  setcc_gen_L,  1)
ropCMOV(LE,  setcc_gen_LE, 0)
ropCMOV(NLE, setcc_gen_LE, 1)
// clang-format on

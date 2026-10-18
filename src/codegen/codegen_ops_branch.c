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
#include "codegen_ops_mov.h"

/* A block compiled with CODEBLOCK_NO_IMMEDIATES belongs to code that keeps being
   rewritten. Code generated at run time, such as the texture mappers of the Looking
   Glass engine, patches branch displacements as well as operands. In such a block a
   taken branch reads its displacement from guest memory, and the displacement is not
   marked as code, so the patch does not invalidate the block. An unrolled loop is laid
   out around its destination at compile time, so it still depends on the bytes. */
static struct {
    codeblock_t *block;
    uint32_t     addr;
    uint32_t     next_pc;
    int          size;
    int          wrap_16;
    int          active;
} jcc_dynamic;

static void
jcc_dynamic_start(codeblock_t *block, uint32_t op_pc, int size, uint32_t next_pc, int wrap_16)
{
    jcc_dynamic.block   = block;
    jcc_dynamic.addr    = cs + op_pc;
    jcc_dynamic.next_pc = next_pc;
    jcc_dynamic.size    = size;
    jcc_dynamic.wrap_16 = wrap_16;
    /* A displacement on another page than the block start is not covered by the
       physical page checks made when the block is looked up. */
    jcc_dynamic.active = (block->flags & CODEBLOCK_NO_IMMEDIATES) && !(((cs + op_pc + size - 1) ^ block->pc) & ~0xfff);
}

static void
jcc_dynamic_end(codeblock_t *block, uint32_t op_pc, int unrolled)
{
    if (!jcc_dynamic.active || unrolled)
        codegen_mark_code_present(block, cs + op_pc, jcc_dynamic.size);
    jcc_dynamic.active = 0;
}

static void
jcc_exit(ir_data_t *ir, uint32_t dest_addr)
{
    if (!jcc_dynamic.active) {
        uop_MOV_IMM(ir, IREG_pc, dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        return;
    }

    switch (jcc_dynamic.size) {
        case 1:
            LOAD_IMMEDIATE_FROM_RAM_8(jcc_dynamic.block, ir, IREG_temp1_B, jcc_dynamic.addr);
            uop_MOVSX(ir, IREG_temp0, IREG_temp1_B);
            break;
        case 2:
            LOAD_IMMEDIATE_FROM_RAM_16(jcc_dynamic.block, ir, IREG_temp1_W, jcc_dynamic.addr);
            uop_MOVSX(ir, IREG_temp0, IREG_temp1_W);
            break;
        default:
            LOAD_IMMEDIATE_FROM_RAM_32(jcc_dynamic.block, ir, IREG_temp0, jcc_dynamic.addr);
            break;
    }
    uop_ADD_IMM(ir, IREG_pc, IREG_temp0, jcc_dynamic.next_pc);
    if (jcc_dynamic.wrap_16)
        uop_AND_IMM(ir, IREG_pc, IREG_pc, 0xffff);
    uop_JMP(ir, codegen_exit_rout);
    /* Registers are allocated in uOP order, so the path that falls through must not
       inherit what this one loaded. */
    uop_NOP_BARRIER(ir);
}

static int
NF_SET_01(void)
{
    return NF_SET() ? 1 : 0;
}
static int
VF_SET_01(void)
{
    return VF_SET() ? 1 : 0;
}

static int
ropJO_common(UNUSED(codeblock_t *block), ir_data_t *ir, uint32_t dest_addr, UNUSED(uint32_t next_pc))
{
    int jump_uop;

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ZN16:
        case FLAGS_ZN32:
            /*Overflow is always zero*/
            return 0;

        case FLAGS_SUB8:
        case FLAGS_DEC8:
            jump_uop = uop_CMP_JNO_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            break;

        case FLAGS_SUB16:
        case FLAGS_DEC16:
            jump_uop = uop_CMP_JNO_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            break;

        case FLAGS_SUB32:
        case FLAGS_DEC32:
            jump_uop = uop_CMP_JNO_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_VF_SET);
            jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
            break;
    }
    jcc_exit(ir, dest_addr);
    uop_set_jump_dest(ir, jump_uop);
    return 0;
}
static int
ropJNO_common(UNUSED(codeblock_t *block), ir_data_t *ir, uint32_t dest_addr, UNUSED(uint32_t next_pc))
{
    int jump_uop;

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ZN16:
        case FLAGS_ZN32:
            /*Overflow is always zero*/
            jcc_exit(ir, dest_addr);
            return 0;

        case FLAGS_SUB8:
        case FLAGS_DEC8:
            jump_uop = uop_CMP_JO_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            break;

        case FLAGS_SUB16:
        case FLAGS_DEC16:
            jump_uop = uop_CMP_JO_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            break;

        case FLAGS_SUB32:
        case FLAGS_DEC32:
            jump_uop = uop_CMP_JO_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_VF_SET);
            jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
            break;
    }
    jcc_exit(ir, dest_addr);
    uop_set_jump_dest(ir, jump_uop);
    return 0;
}

static int
ropJB_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
    int jump_uop;
    int do_unroll = (CF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr));

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ZN16:
        case FLAGS_ZN32:
            /*Carry is always zero*/
            return 0;

        case FLAGS_SUB8:
            if (do_unroll)
                jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            else
                jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            break;

        case FLAGS_SUB16:
            if (do_unroll)
                jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            else
                jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            break;

        case FLAGS_SUB32:
            if (do_unroll)
                jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            else
                jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            break;

        case FLAGS_ADD8:
        case FLAGS_ADD16:
        case FLAGS_ADD32:
            /*CF = (res < op1) -- unsigned overflow, works for all sizes since MOVZX'd*/
            if (do_unroll)
                jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_res, IREG_flags_op1);
            else
                jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_res, IREG_flags_op1);
            break;

        case FLAGS_INC8:
        case FLAGS_INC16:
        case FLAGS_INC32:
        case FLAGS_DEC8:
        case FLAGS_DEC16:
        case FLAGS_DEC32:
            /*INC/DEC preserve CF in cpu_state.flags*/
            uop_AND_IMM(ir, IREG_temp0_W, IREG_flags, C_FLAG);
            if (do_unroll)
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0_W, 0);
            else
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0_W, 0);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_CF_SET);
            if (do_unroll)
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
            else
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
            break;
    }
    if (do_unroll) {
        uop_MOV_IMM(ir, IREG_pc, next_pc);
        uop_JMP(ir, codegen_exit_rout);
    } else
        jcc_exit(ir, dest_addr);
    uop_set_jump_dest(ir, jump_uop);
    return do_unroll ? 1 : 0;
}
static int
ropJNB_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
    int jump_uop;
    int do_unroll = (!CF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr));

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ZN16:
        case FLAGS_ZN32:
            /*Carry is always zero*/
            jcc_exit(ir, dest_addr);
            return 0;

        case FLAGS_SUB8:
            if (do_unroll)
                jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            else
                jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            break;

        case FLAGS_SUB16:
            if (do_unroll)
                jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            else
                jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            break;

        case FLAGS_SUB32:
            if (do_unroll)
                jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            else
                jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            break;

        case FLAGS_ADD8:
        case FLAGS_ADD16:
        case FLAGS_ADD32:
            /*CF = (res < op1) -- unsigned overflow, works for all sizes since MOVZX'd*/
            if (do_unroll)
                jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_res, IREG_flags_op1);
            else
                jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_res, IREG_flags_op1);
            break;

        case FLAGS_INC8:
        case FLAGS_INC16:
        case FLAGS_INC32:
        case FLAGS_DEC8:
        case FLAGS_DEC16:
        case FLAGS_DEC32:
            /*INC/DEC preserve CF in cpu_state.flags*/
            uop_AND_IMM(ir, IREG_temp0_W, IREG_flags, C_FLAG);
            if (do_unroll)
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0_W, 0);
            else
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0_W, 0);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_CF_SET);
            if (do_unroll)
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
            else
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
            break;
    }
    if (do_unroll) {
        uop_MOV_IMM(ir, IREG_pc, next_pc);
        uop_JMP(ir, codegen_exit_rout);
    } else
        jcc_exit(ir, dest_addr);
    uop_set_jump_dest(ir, jump_uop);
    return do_unroll ? 1 : 0;
}

/* Temporarily disable the unrolling of JZ/JNZ due to the code sometimes taking the wrong turn. */
static int
ropJE_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
    int jump_uop;

    if (ZF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr)) {
        if (!codegen_flags_changed || !flags_res_valid()) {
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
            jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
        } else {
            jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_flags_res, 0);
        }
        uop_MOV_IMM(ir, IREG_pc, next_pc);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return 1;
    } else {
        if (!codegen_flags_changed || !flags_res_valid()) {
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
            jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
        } else {
            jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_flags_res, 0);
        }
        jcc_exit(ir, dest_addr);
        uop_set_jump_dest(ir, jump_uop);
    }
    return 0;
}
int
ropJNE_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
    int jump_uop;

    if (!ZF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr)) {
        if (!codegen_flags_changed || !flags_res_valid()) {
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
            jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
        } else {
            jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_flags_res, 0);
        }
        uop_MOV_IMM(ir, IREG_pc, next_pc);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return 1;
    } else {
        if (!codegen_flags_changed || !flags_res_valid()) {
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
            jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
        } else {
            jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_flags_res, 0);
        }
        jcc_exit(ir, dest_addr);
        uop_set_jump_dest(ir, jump_uop);
    }
    return 0;
}

static int
ropJBE_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
    int jump_uop;
    int jump_uop2 = -1;
    int do_unroll = ((CF_SET() || ZF_SET()) && codegen_can_unroll(block, ir, next_pc, dest_addr));

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ZN16:
        case FLAGS_ZN32:
            /*Carry is always zero, so test zero only*/
            if (do_unroll)
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_flags_res, 0);
            else
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_flags_res, 0);
            break;

        case FLAGS_SUB8:
            if (do_unroll)
                jump_uop = uop_CMP_JBE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            else
                jump_uop = uop_CMP_JNBE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            break;
        case FLAGS_SUB16:
            if (do_unroll)
                jump_uop = uop_CMP_JBE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            else
                jump_uop = uop_CMP_JNBE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            break;
        case FLAGS_SUB32:
            if (do_unroll)
                jump_uop = uop_CMP_JBE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            else
                jump_uop = uop_CMP_JNBE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            break;

        case FLAGS_INC8:
        case FLAGS_INC16:
        case FLAGS_INC32:
        case FLAGS_DEC8:
        case FLAGS_DEC16:
        case FLAGS_DEC32:
            /*INC/DEC preserve CF in cpu_state.flags; test CF then ZF from res*/
            uop_AND_IMM(ir, IREG_temp0_W, IREG_flags, C_FLAG);
            if (do_unroll) {
                jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0_W, 0);
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_flags_res, 0);
            } else {
                jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0_W, 0);
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_flags_res, 0);
            }
            break;

        case FLAGS_UNKNOWN:
        default:
            if (do_unroll) {
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_CF_SET);
                jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
            } else {
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_CF_SET);
                jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
            }
            break;
    }
    if (do_unroll) {
        uop_MOV_IMM(ir, IREG_pc, next_pc);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        if (jump_uop2 != -1)
            uop_set_jump_dest(ir, jump_uop2);
        return 1;
    } else {
        if (jump_uop2 != -1)
            uop_set_jump_dest(ir, jump_uop2);
        jcc_exit(ir, dest_addr);
        uop_set_jump_dest(ir, jump_uop);
        return 0;
    }
}
static int
ropJNBE_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
    int jump_uop;
    int jump_uop2 = -1;
    int do_unroll = ((!CF_SET() && !ZF_SET()) && codegen_can_unroll(block, ir, next_pc, dest_addr));

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ZN16:
        case FLAGS_ZN32:
            /*Carry is always zero, so test zero only*/
            if (do_unroll)
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_flags_res, 0);
            else
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_flags_res, 0);
            break;

        case FLAGS_SUB8:
            if (do_unroll)
                jump_uop = uop_CMP_JNBE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            else
                jump_uop = uop_CMP_JBE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            break;
        case FLAGS_SUB16:
            if (do_unroll)
                jump_uop = uop_CMP_JNBE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            else
                jump_uop = uop_CMP_JBE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            break;
        case FLAGS_SUB32:
            if (do_unroll)
                jump_uop = uop_CMP_JNBE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            else
                jump_uop = uop_CMP_JBE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            break;

        case FLAGS_INC8:
        case FLAGS_INC16:
        case FLAGS_INC32:
        case FLAGS_DEC8:
        case FLAGS_DEC16:
        case FLAGS_DEC32:
            /*INC/DEC preserve CF in cpu_state.flags; test CF then ZF from res*/
            uop_AND_IMM(ir, IREG_temp0_W, IREG_flags, C_FLAG);
            if (do_unroll) {
                jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0_W, 0);
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_flags_res, 0);
            } else {
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0_W, 0);
                jump_uop2 = uop_CMP_IMM_JZ_DEST(ir, IREG_flags_res, 0);
            }
            break;

        case FLAGS_UNKNOWN:
        default:
            if (do_unroll) {
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_CF_SET);
                jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
            } else {
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_CF_SET);
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
                jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
            }
            break;
    }
    if (do_unroll) {
        if (jump_uop2 != -1)
            uop_set_jump_dest(ir, jump_uop2);
        uop_MOV_IMM(ir, IREG_pc, next_pc);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return 1;
    } else {
        jcc_exit(ir, dest_addr);
        uop_set_jump_dest(ir, jump_uop);
        if (jump_uop2 != -1)
            uop_set_jump_dest(ir, jump_uop2);
        return 0;
    }
}

static int
ropJS_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
    int jump_uop;
    int do_unroll = (NF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr));

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ADD8:
        case FLAGS_SUB8:
        case FLAGS_SHL8:
        case FLAGS_SHR8:
        case FLAGS_SAR8:
        case FLAGS_INC8:
        case FLAGS_DEC8:
            if (do_unroll)
                jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_B);
            else
                jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_B);
            break;

        case FLAGS_ZN16:
        case FLAGS_ADD16:
        case FLAGS_SUB16:
        case FLAGS_SHL16:
        case FLAGS_SHR16:
        case FLAGS_SAR16:
        case FLAGS_INC16:
        case FLAGS_DEC16:
            if (do_unroll)
                jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_W);
            else
                jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_W);
            break;

        case FLAGS_ZN32:
        case FLAGS_ADD32:
        case FLAGS_SUB32:
        case FLAGS_SHL32:
        case FLAGS_SHR32:
        case FLAGS_SAR32:
        case FLAGS_INC32:
        case FLAGS_DEC32:
            if (do_unroll)
                jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res);
            else
                jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_NF_SET);
            if (do_unroll)
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
            else
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
            break;
    }
    if (do_unroll) {
        uop_MOV_IMM(ir, IREG_pc, next_pc);
        uop_JMP(ir, codegen_exit_rout);
    } else
        jcc_exit(ir, dest_addr);
    uop_set_jump_dest(ir, jump_uop);
    return do_unroll ? 1 : 0;
}
static int
ropJNS_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
    int jump_uop;
    int do_unroll = (!NF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr));

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
        case FLAGS_ADD8:
        case FLAGS_SUB8:
        case FLAGS_SHL8:
        case FLAGS_SHR8:
        case FLAGS_SAR8:
        case FLAGS_INC8:
        case FLAGS_DEC8:
            if (do_unroll)
                jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_B);
            else
                jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_B);
            break;

        case FLAGS_ZN16:
        case FLAGS_ADD16:
        case FLAGS_SUB16:
        case FLAGS_SHL16:
        case FLAGS_SHR16:
        case FLAGS_SAR16:
        case FLAGS_INC16:
        case FLAGS_DEC16:
            if (do_unroll)
                jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_W);
            else
                jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_W);
            break;

        case FLAGS_ZN32:
        case FLAGS_ADD32:
        case FLAGS_SUB32:
        case FLAGS_SHL32:
        case FLAGS_SHR32:
        case FLAGS_SAR32:
        case FLAGS_INC32:
        case FLAGS_DEC32:
            if (do_unroll)
                jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res);
            else
                jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_NF_SET);
            if (do_unroll)
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
            else
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
            break;
    }
    if (do_unroll) {
        uop_MOV_IMM(ir, IREG_pc, next_pc);
        uop_JMP(ir, codegen_exit_rout);
    } else
        jcc_exit(ir, dest_addr);
    uop_set_jump_dest(ir, jump_uop);
    return do_unroll ? 1 : 0;
}

static int
ropJP_common(UNUSED(codeblock_t *block), ir_data_t *ir, uint32_t dest_addr, UNUSED(uint32_t next_pc))
{
    int jump_uop;

    uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_PF_SET);
    jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
    jcc_exit(ir, dest_addr);
    uop_set_jump_dest(ir, jump_uop);
    return 0;
}
static int
ropJNP_common(UNUSED(codeblock_t *block), ir_data_t *ir, uint32_t dest_addr, UNUSED(uint32_t next_pc))
{
    int jump_uop;

    uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_PF_SET);
    jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
    jcc_exit(ir, dest_addr);
    uop_set_jump_dest(ir, jump_uop);
    return 0;
}

static int
ropJL_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
    int jump_uop;
    int do_unroll = ((NF_SET() ? 1 : 0) != (VF_SET() ? 1 : 0) && codegen_can_unroll(block, ir, next_pc, dest_addr));

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
            /*V flag is always clear. Condition is true if N is set*/
            if (do_unroll)
                jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_B);
            else
                jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_B);
            break;
        case FLAGS_ZN16:
            if (do_unroll)
                jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_W);
            else
                jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_W);
            break;
        case FLAGS_ZN32:
            if (do_unroll)
                jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res);
            else
                jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res);
            break;

        case FLAGS_SUB8:
        case FLAGS_DEC8:
            if (do_unroll)
                jump_uop = uop_CMP_JL_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            else
                jump_uop = uop_CMP_JNL_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            break;
        case FLAGS_SUB16:
        case FLAGS_DEC16:
            if (do_unroll)
                jump_uop = uop_CMP_JL_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            else
                jump_uop = uop_CMP_JNL_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            break;
        case FLAGS_SUB32:
        case FLAGS_DEC32:
            if (do_unroll)
                jump_uop = uop_CMP_JL_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            else
                jump_uop = uop_CMP_JNL_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, NF_SET_01);
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp1, VF_SET_01);
            if (do_unroll)
                jump_uop = uop_CMP_JNZ_DEST(ir, IREG_temp0, IREG_temp1);
            else
                jump_uop = uop_CMP_JZ_DEST(ir, IREG_temp0, IREG_temp1);
            break;
    }
    if (do_unroll) {
        uop_MOV_IMM(ir, IREG_pc, next_pc);
        uop_JMP(ir, codegen_exit_rout);
    } else
        jcc_exit(ir, dest_addr);
    uop_set_jump_dest(ir, jump_uop);
    return do_unroll ? 1 : 0;
}
static int
ropJNL_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
    int jump_uop;
    int do_unroll = ((NF_SET() ? 1 : 0) == (VF_SET() ? 1 : 0) && codegen_can_unroll(block, ir, next_pc, dest_addr));

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_ZN8:
            /*V flag is always clear. Condition is true if N is set*/
            if (do_unroll)
                jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_B);
            else
                jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_B);
            break;
        case FLAGS_ZN16:
            if (do_unroll)
                jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_W);
            else
                jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_W);
            break;
        case FLAGS_ZN32:
            if (do_unroll)
                jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res);
            else
                jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res);
            break;

        case FLAGS_SUB8:
        case FLAGS_DEC8:
            if (do_unroll)
                jump_uop = uop_CMP_JNL_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            else
                jump_uop = uop_CMP_JL_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            break;
        case FLAGS_SUB16:
        case FLAGS_DEC16:
            if (do_unroll)
                jump_uop = uop_CMP_JNL_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            else
                jump_uop = uop_CMP_JL_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            break;
        case FLAGS_SUB32:
        case FLAGS_DEC32:
            if (do_unroll)
                jump_uop = uop_CMP_JNL_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            else
                jump_uop = uop_CMP_JL_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            break;

        case FLAGS_UNKNOWN:
        default:
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, NF_SET_01);
            uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp1, VF_SET_01);
            if (do_unroll)
                jump_uop = uop_CMP_JZ_DEST(ir, IREG_temp0, IREG_temp1);
            else
                jump_uop = uop_CMP_JNZ_DEST(ir, IREG_temp0, IREG_temp1);
            break;
    }
    if (do_unroll) {
        uop_MOV_IMM(ir, IREG_pc, next_pc);
        uop_JMP(ir, codegen_exit_rout);
    } else
        jcc_exit(ir, dest_addr);
    uop_set_jump_dest(ir, jump_uop);
    return do_unroll ? 1 : 0;
}

static int
ropJLE_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
    int jump_uop;
    int jump_uop2 = -1;
    int do_unroll = (((NF_SET() ? 1 : 0) != (VF_SET() ? 1 : 0) || ZF_SET()) && codegen_can_unroll(block, ir, next_pc, dest_addr));

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_SUB8:
        case FLAGS_DEC8:
            if (do_unroll)
                jump_uop = uop_CMP_JLE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            else
                jump_uop = uop_CMP_JNLE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            break;
        case FLAGS_SUB16:
        case FLAGS_DEC16:
            if (do_unroll)
                jump_uop = uop_CMP_JLE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            else
                jump_uop = uop_CMP_JNLE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            break;
        case FLAGS_SUB32:
        case FLAGS_DEC32:
            if (do_unroll)
                jump_uop = uop_CMP_JLE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            else
                jump_uop = uop_CMP_JNLE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            break;

        case FLAGS_UNKNOWN:
        default:
            if (do_unroll) {
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
                jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, NF_SET_01);
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp1, VF_SET_01);
                jump_uop = uop_CMP_JNZ_DEST(ir, IREG_temp0, IREG_temp1);
            } else {
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
                jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, NF_SET_01);
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp1, VF_SET_01);
                jump_uop = uop_CMP_JZ_DEST(ir, IREG_temp0, IREG_temp1);
            }
            break;
    }
    if (do_unroll) {
        uop_MOV_IMM(ir, IREG_pc, next_pc);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        if (jump_uop2 != -1)
            uop_set_jump_dest(ir, jump_uop2);
        return 1;
    } else {
        if (jump_uop2 != -1)
            uop_set_jump_dest(ir, jump_uop2);
        jcc_exit(ir, dest_addr);
        uop_set_jump_dest(ir, jump_uop);
        return 0;
    }
}
static int
ropJNLE_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
    int jump_uop;
    int jump_uop2 = -1;
    int do_unroll = ((NF_SET() ? 1 : 0) == (VF_SET() ? 1 : 0) && !ZF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr));

    switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN) {
        case FLAGS_SUB8:
        case FLAGS_DEC8:
            if (do_unroll)
                jump_uop = uop_CMP_JNLE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            else
                jump_uop = uop_CMP_JLE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
            break;
        case FLAGS_SUB16:
        case FLAGS_DEC16:
            if (do_unroll)
                jump_uop = uop_CMP_JNLE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            else
                jump_uop = uop_CMP_JLE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
            break;
        case FLAGS_SUB32:
        case FLAGS_DEC32:
            if (do_unroll)
                jump_uop = uop_CMP_JNLE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            else
                jump_uop = uop_CMP_JLE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
            break;

        case FLAGS_UNKNOWN:
        default:
            if (do_unroll) {
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
                jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, NF_SET_01);
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp1, VF_SET_01);
                jump_uop = uop_CMP_JZ_DEST(ir, IREG_temp0, IREG_temp1);
            } else {
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
                jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, NF_SET_01);
                uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp1, VF_SET_01);
                jump_uop = uop_CMP_JNZ_DEST(ir, IREG_temp0, IREG_temp1);
            }
            break;
    }
    if (do_unroll) {
        if (jump_uop2 != -1)
            uop_set_jump_dest(ir, jump_uop2);
        uop_MOV_IMM(ir, IREG_pc, next_pc);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return 1;
    } else {
        jcc_exit(ir, dest_addr);
        uop_set_jump_dest(ir, jump_uop);
        if (jump_uop2 != -1)
            uop_set_jump_dest(ir, jump_uop2);
        return 0;
    }
}

#define ropJ(cond)                                                      \
    uint32_t ropJ##cond##_8(codeblock_t *block,                         \
                            ir_data_t *ir,                              \
                            UNUSED(uint8_t opcode),                     \
                            UNUSED(uint32_t fetchdat),                  \
                            uint32_t op_32,                             \
                            uint32_t op_pc)                             \
    {                                                                   \
        uint32_t offset    = (int32_t) (int8_t) fastreadb(cs + op_pc);  \
        uint32_t dest_addr = op_pc + 1 + offset;                        \
        int      ret;                                                   \
                                                                        \
        if (!(op_32 & 0x100))                                           \
            dest_addr &= 0xffff;                                        \
        jcc_dynamic_start(block, op_pc, 1, op_pc + 1,                   \
                          !(op_32 & 0x100));                            \
        ret = ropJ##cond##_common(block, ir, dest_addr, op_pc + 1);     \
        jcc_dynamic_end(block, op_pc, ret);                             \
                                                                        \
        return ret ? dest_addr : (op_pc + 1);                           \
    }                                                                   \
    uint32_t ropJ##cond##_16(codeblock_t *block,                        \
                             ir_data_t *ir,                             \
                             UNUSED(uint8_t opcode),                    \
                             UNUSED(uint32_t fetchdat),                 \
                             UNUSED(uint32_t op_32),                    \
                             uint32_t op_pc)                            \
    {                                                                   \
        uint32_t offset    = (int32_t) (int16_t) fastreadw(cs + op_pc); \
        uint32_t dest_addr = (op_pc + 2 + offset) & 0xffff;             \
        int      ret;                                                   \
                                                                        \
        jcc_dynamic_start(block, op_pc, 2, op_pc + 2, 1);               \
        ret = ropJ##cond##_common(block, ir, dest_addr, op_pc + 2);     \
        jcc_dynamic_end(block, op_pc, ret);                             \
                                                                        \
        return ret ? dest_addr : (op_pc + 2);                           \
    }                                                                   \
    uint32_t ropJ##cond##_32(codeblock_t *block,                        \
                             ir_data_t *ir,                             \
                             UNUSED(uint8_t opcode),                    \
                             UNUSED(uint32_t fetchdat),                 \
                             UNUSED(uint32_t op_32),                    \
                             uint32_t op_pc)                            \
    {                                                                   \
        uint32_t offset    = fastreadl(cs + op_pc);                     \
        uint32_t dest_addr = op_pc + 4 + offset;                        \
        int      ret;                                                   \
                                                                        \
        jcc_dynamic_start(block, op_pc, 4, op_pc + 4, 0);               \
        ret = ropJ##cond##_common(block, ir, dest_addr, op_pc + 4);     \
        jcc_dynamic_end(block, op_pc, ret);                             \
                                                                        \
        return ret ? dest_addr : (op_pc + 4);                           \
    }

// clang-format off
ropJ(O)
ropJ(NO)
ropJ(B)
ropJ(NB)
ropJ(E)
ropJ(NE)
ropJ(BE)
ropJ(NBE)
ropJ(S)
ropJ(NS)
ropJ(P)
ropJ(NP)
ropJ(L)
ropJ(NL)
ropJ(LE)
ropJ(NLE)
// clang-format on

uint32_t
ropJCXZ(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t offset    = (int32_t) (int8_t) fastreadb(cs + op_pc);
    uint32_t dest_addr = op_pc + 1 + offset;
    int      jump_uop;

    if (!(op_32 & 0x100))
        dest_addr &= 0xffff;

    if (op_32 & 0x200)
        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_ECX, 0);
    else
        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_CX, 0);
    uop_MOV_IMM(ir, IREG_pc, dest_addr);
    uop_JMP(ir, codegen_exit_rout);
    uop_set_jump_dest(ir, jump_uop);

    codegen_mark_code_present(block, cs + op_pc, 1);
    return op_pc + 1;
}

uint32_t
ropLOOP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t offset    = (int32_t) (int8_t) fastreadb(cs + op_pc);
    uint32_t dest_addr = op_pc + 1 + offset;
    uint32_t ret_addr;
    int      jump_uop;

    if (!(op_32 & 0x100))
        dest_addr &= 0xffff;

    if (((op_32 & 0x200) ? ECX : CX) != 1 && codegen_can_unroll(block, ir, op_pc + 1, dest_addr)) {
        if (op_32 & 0x200) {
            uop_SUB_IMM(ir, IREG_ECX, IREG_ECX, 1);
            jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_ECX, 0);
        } else {
            uop_SUB_IMM(ir, IREG_CX, IREG_CX, 1);
            jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_CX, 0);
        }
        uop_MOV_IMM(ir, IREG_pc, op_pc + 1);
        ret_addr = dest_addr;
        CPU_BLOCK_END();
    } else {
        if (op_32 & 0x200) {
            uop_SUB_IMM(ir, IREG_ECX, IREG_ECX, 1);
            jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_ECX, 0);
        } else {
            uop_SUB_IMM(ir, IREG_CX, IREG_CX, 1);
            jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_CX, 0);
        }
        uop_MOV_IMM(ir, IREG_pc, dest_addr);
        ret_addr = op_pc + 1;
    }
    uop_JMP(ir, codegen_exit_rout);
    uop_set_jump_dest(ir, jump_uop);

    codegen_mark_code_present(block, cs + op_pc, 1);
    return ret_addr;
}

uint32_t
ropLOOPE(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t offset    = (int32_t) (int8_t) fastreadb(cs + op_pc);
    uint32_t dest_addr = op_pc + 1 + offset;
    int      jump_uop;
    int      jump_uop2;

    if (!(op_32 & 0x100))
        dest_addr &= 0xffff;

    if (op_32 & 0x200) {
        uop_SUB_IMM(ir, IREG_ECX, IREG_ECX, 1);
        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_ECX, 0);
    } else {
        uop_SUB_IMM(ir, IREG_CX, IREG_CX, 1);
        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_CX, 0);
    }
    if (!codegen_flags_changed || !flags_res_valid()) {
        uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
        jump_uop2 = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
    } else {
        jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_flags_res, 0);
    }
    uop_MOV_IMM(ir, IREG_pc, dest_addr);
    uop_JMP(ir, codegen_exit_rout);
    uop_NOP_BARRIER(ir);
    uop_set_jump_dest(ir, jump_uop);
    uop_set_jump_dest(ir, jump_uop2);

    codegen_mark_code_present(block, cs + op_pc, 1);
    return op_pc + 1;
}
uint32_t
ropLOOPNE(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t offset    = (int32_t) (int8_t) fastreadb(cs + op_pc);
    uint32_t dest_addr = op_pc + 1 + offset;
    int      jump_uop;
    int      jump_uop2;

    if (!(op_32 & 0x100))
        dest_addr &= 0xffff;

    if (op_32 & 0x200) {
        uop_SUB_IMM(ir, IREG_ECX, IREG_ECX, 1);
        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_ECX, 0);
    } else {
        uop_SUB_IMM(ir, IREG_CX, IREG_CX, 1);
        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_CX, 0);
    }
    if (!codegen_flags_changed || !flags_res_valid()) {
        uop_CALL_FUNC_RESULT_ORDERED(ir, IREG_temp0, jit_ZF_SET);
        jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
    } else {
        jump_uop2 = uop_CMP_IMM_JZ_DEST(ir, IREG_flags_res, 0);
    }
    uop_MOV_IMM(ir, IREG_pc, dest_addr);
    uop_JMP(ir, codegen_exit_rout);
    uop_NOP_BARRIER(ir);
    uop_set_jump_dest(ir, jump_uop);
    uop_set_jump_dest(ir, jump_uop2);

    codegen_mark_code_present(block, cs + op_pc, 1);
    return op_pc + 1;
}

/*SETcc. The condition is worked out the way the Jcc above work it out, but
  into IREG_temp0 as 0 or 1 instead of into a jump, so nothing is flushed
  unless the flags have to come from a function call anyway. cc is the low
  nibble of the opcode; odd codes are the opposite of the even one below.*/
static void
setcc_value(ir_data_t *ir, int cc)
{
    int neg     = cc & 1;
    int flags_op = codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN;

    switch (cc & ~1) {
        case UOP_CC_O:
            switch (flags_op) {
                case FLAGS_ZN8:
                case FLAGS_ZN16:
                case FLAGS_ZN32:
                    /*Overflow is always zero*/
                    uop_MOV_IMM(ir, IREG_temp0, neg);
                    return;
                case FLAGS_SUB8:
                case FLAGS_DEC8:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1_B, IREG_flags_op2_B, UOP_CC_O ^ neg);
                    return;
                case FLAGS_SUB16:
                case FLAGS_DEC16:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1_W, IREG_flags_op2_W, UOP_CC_O ^ neg);
                    return;
                case FLAGS_SUB32:
                case FLAGS_DEC32:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1, IREG_flags_op2, UOP_CC_O ^ neg);
                    return;
                default:
                    uop_CALL_FUNC_RESULT(ir, IREG_temp0, jit_VF_SET);
                    uop_TEST_SET(ir, IREG_temp0, IREG_temp0, UOP_CC_NZ ^ neg);
                    return;
            }

        case UOP_CC_B:
            switch (flags_op) {
                case FLAGS_ZN8:
                case FLAGS_ZN16:
                case FLAGS_ZN32:
                    /*Carry is always zero*/
                    uop_MOV_IMM(ir, IREG_temp0, neg);
                    return;
                case FLAGS_SUB8:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1_B, IREG_flags_op2_B, UOP_CC_B ^ neg);
                    return;
                case FLAGS_SUB16:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1_W, IREG_flags_op2_W, UOP_CC_B ^ neg);
                    return;
                case FLAGS_SUB32:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1, IREG_flags_op2, UOP_CC_B ^ neg);
                    return;
                default:
                    uop_CALL_FUNC_RESULT(ir, IREG_temp0, jit_CF_SET);
                    uop_TEST_SET(ir, IREG_temp0, IREG_temp0, UOP_CC_NZ ^ neg);
                    return;
            }

        case UOP_CC_Z:
            if (codegen_flags_changed && flags_res_valid())
                uop_TEST_SET(ir, IREG_temp0, IREG_flags_res, UOP_CC_Z ^ neg);
            else {
                uop_CALL_FUNC_RESULT(ir, IREG_temp0, jit_ZF_SET);
                uop_TEST_SET(ir, IREG_temp0, IREG_temp0, UOP_CC_NZ ^ neg);
            }
            return;

        case UOP_CC_BE:
            switch (flags_op) {
                case FLAGS_ZN8:
                case FLAGS_ZN16:
                case FLAGS_ZN32:
                    /*Carry is always zero, so zero only*/
                    uop_TEST_SET(ir, IREG_temp0, IREG_flags_res, UOP_CC_Z ^ neg);
                    return;
                case FLAGS_SUB8:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1_B, IREG_flags_op2_B, UOP_CC_BE ^ neg);
                    return;
                case FLAGS_SUB16:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1_W, IREG_flags_op2_W, UOP_CC_BE ^ neg);
                    return;
                case FLAGS_SUB32:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1, IREG_flags_op2, UOP_CC_BE ^ neg);
                    return;
                default:
                    uop_CALL_FUNC_RESULT(ir, IREG_temp1, jit_CF_SET);
                    uop_CALL_FUNC_RESULT(ir, IREG_temp2, jit_ZF_SET);
                    uop_OR(ir, IREG_temp1, IREG_temp1, IREG_temp2);
                    uop_TEST_SET(ir, IREG_temp0, IREG_temp1, UOP_CC_NZ ^ neg);
                    return;
            }

        case UOP_CC_S:
            switch (flags_op) {
                case FLAGS_ZN8:
                case FLAGS_ADD8:
                case FLAGS_SUB8:
                case FLAGS_SHL8:
                case FLAGS_SHR8:
                case FLAGS_SAR8:
                case FLAGS_INC8:
                case FLAGS_DEC8:
                    uop_TEST_SET(ir, IREG_temp0, IREG_flags_res_B, UOP_CC_S ^ neg);
                    return;
                case FLAGS_ZN16:
                case FLAGS_ADD16:
                case FLAGS_SUB16:
                case FLAGS_SHL16:
                case FLAGS_SHR16:
                case FLAGS_SAR16:
                case FLAGS_INC16:
                case FLAGS_DEC16:
                    uop_TEST_SET(ir, IREG_temp0, IREG_flags_res_W, UOP_CC_S ^ neg);
                    return;
                case FLAGS_ZN32:
                case FLAGS_ADD32:
                case FLAGS_SUB32:
                case FLAGS_SHL32:
                case FLAGS_SHR32:
                case FLAGS_SAR32:
                case FLAGS_INC32:
                case FLAGS_DEC32:
                    uop_TEST_SET(ir, IREG_temp0, IREG_flags_res, UOP_CC_S ^ neg);
                    return;
                default:
                    uop_CALL_FUNC_RESULT(ir, IREG_temp0, jit_NF_SET);
                    uop_TEST_SET(ir, IREG_temp0, IREG_temp0, UOP_CC_NZ ^ neg);
                    return;
            }

        case 0xa: /*P*/
            uop_CALL_FUNC_RESULT(ir, IREG_temp0, jit_PF_SET);
            uop_TEST_SET(ir, IREG_temp0, IREG_temp0, UOP_CC_NZ ^ neg);
            return;

        case UOP_CC_L:
            switch (flags_op) {
                case FLAGS_ZN8:
                    /*V is always clear, so the condition is N*/
                    uop_TEST_SET(ir, IREG_temp0, IREG_flags_res_B, UOP_CC_S ^ neg);
                    return;
                case FLAGS_ZN16:
                    uop_TEST_SET(ir, IREG_temp0, IREG_flags_res_W, UOP_CC_S ^ neg);
                    return;
                case FLAGS_ZN32:
                    uop_TEST_SET(ir, IREG_temp0, IREG_flags_res, UOP_CC_S ^ neg);
                    return;
                case FLAGS_SUB8:
                case FLAGS_DEC8:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1_B, IREG_flags_op2_B, UOP_CC_L ^ neg);
                    return;
                case FLAGS_SUB16:
                case FLAGS_DEC16:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1_W, IREG_flags_op2_W, UOP_CC_L ^ neg);
                    return;
                case FLAGS_SUB32:
                case FLAGS_DEC32:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1, IREG_flags_op2, UOP_CC_L ^ neg);
                    return;
                default:
                    uop_CALL_FUNC_RESULT(ir, IREG_temp1, NF_SET_01);
                    uop_CALL_FUNC_RESULT(ir, IREG_temp2, VF_SET_01);
                    uop_CMP_SET(ir, IREG_temp0, IREG_temp1, IREG_temp2, UOP_CC_NZ ^ neg);
                    return;
            }

        case UOP_CC_LE:
        default:
            switch (flags_op) {
                case FLAGS_SUB8:
                case FLAGS_DEC8:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1_B, IREG_flags_op2_B, UOP_CC_LE ^ neg);
                    return;
                case FLAGS_SUB16:
                case FLAGS_DEC16:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1_W, IREG_flags_op2_W, UOP_CC_LE ^ neg);
                    return;
                case FLAGS_SUB32:
                case FLAGS_DEC32:
                    uop_CMP_SET(ir, IREG_temp0, IREG_flags_op1, IREG_flags_op2, UOP_CC_LE ^ neg);
                    return;
                default:
                    /*Z, or N != V*/
                    uop_CALL_FUNC_RESULT(ir, IREG_temp1, NF_SET_01);
                    uop_CALL_FUNC_RESULT(ir, IREG_temp2, VF_SET_01);
                    uop_CALL_FUNC_RESULT(ir, IREG_temp3, jit_ZF_SET);
                    uop_XOR(ir, IREG_temp1, IREG_temp1, IREG_temp2);
                    uop_OR(ir, IREG_temp1, IREG_temp1, IREG_temp3);
                    uop_TEST_SET(ir, IREG_temp0, IREG_temp1, UOP_CC_NZ ^ neg);
                    return;
            }
    }
}

uint32_t
ropSETcc(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
#ifndef CODEGEN_BACKEND_HAS_INT_UOPS
    return 0;
#endif
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        setcc_value(ir, opcode & 0xf);
        uop_MOV(ir, IREG_8(fetchdat & 7), IREG_temp0_B);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        setcc_value(ir, opcode & 0xf);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
    }

    return op_pc + 1;
}

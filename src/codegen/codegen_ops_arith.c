#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86_flags.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_arith.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_jit_wrappers.h"

static inline void
get_cf(ir_data_t *ir, int dest_reg)
{
    uop_CALL_FUNC_RESULT(ir, dest_reg, jit_CF_SET);
}

uint32_t
ropADC_AL_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    get_cf(ir, IREG_temp0);
    uop_MOVZX(ir, IREG_flags_op1, IREG_AL);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp1_B, cs + op_pc);
        uop_ADD(ir, IREG_AL, IREG_AL, IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp1_B);
    } else {
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_ADD_IMM(ir, IREG_AL, IREG_AL, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 1);
    }
    uop_ADD(ir, IREG_AL, IREG_AL, IREG_temp0_B);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC8);
    uop_MOVZX(ir, IREG_flags_res, IREG_AL);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADC_AX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    get_cf(ir, IREG_temp0);
    uop_MOVZX(ir, IREG_flags_op1, IREG_AX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp1_W, cs + op_pc);
        uop_ADD(ir, IREG_AX, IREG_AX, IREG_temp1_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp1_W);
    } else {
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_ADD_IMM(ir, IREG_AX, IREG_AX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 2);
    }
    uop_ADD(ir, IREG_AX, IREG_AX, IREG_temp0_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC16);
    uop_MOVZX(ir, IREG_flags_res, IREG_AX);

    codegen_flags_changed = 1;
    return op_pc + 2;
}
uint32_t
ropADC_EAX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    get_cf(ir, IREG_temp0);
    uop_MOV(ir, IREG_flags_op1, IREG_EAX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp1, cs + op_pc);
        uop_ADD(ir, IREG_EAX, IREG_EAX, IREG_temp1);
        uop_MOV(ir, IREG_flags_op2, IREG_temp1);
    } else {
        uint32_t imm_data = fastreadl(cs + op_pc);

        uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 4);
    }
    uop_ADD(ir, IREG_EAX, IREG_EAX, IREG_temp0);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC32);
    uop_MOV(ir, IREG_flags_res, IREG_EAX);

    codegen_flags_changed = 1;
    return op_pc + 4;
}
uint32_t
ropADC_b_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
    }

    uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp1_B);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC8);
    uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADC_b_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);

        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_ADD(ir, IREG_temp2_B, IREG_temp0_B, IREG_8(src_reg));
        uop_ADD(ir, IREG_temp2_B, IREG_temp2_B, IREG_temp1_B);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp2_B);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp2_B);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC8);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADC_w_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
    }

    uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC16);
    uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADC_w_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);

        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_ADD(ir, IREG_temp2_W, IREG_temp0_W, IREG_16(src_reg));
        uop_ADD(ir, IREG_temp2_W, IREG_temp2_W, IREG_temp1_W);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp2_W);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp2_W);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC16);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADC_l_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_flags_op2, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_flags_op2);
    }

    uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC32);
    uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADC_l_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);

        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_ADD(ir, IREG_temp2, IREG_temp0, IREG_32(src_reg));
        uop_ADD(ir, IREG_temp2, IREG_temp2, IREG_temp1);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp2);
        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_MOV(ir, IREG_flags_res, IREG_temp2);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC32);

    codegen_flags_changed = 1;
    return op_pc + 1;
}

uint32_t
ropADD_AL_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVZX(ir, IREG_flags_op1, IREG_AL);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0_B, cs + op_pc);
        uop_ADD(ir, IREG_AL, IREG_AL, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
    } else {
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_ADD_IMM(ir, IREG_AL, IREG_AL, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 1);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD8);
    uop_MOVZX(ir, IREG_flags_res, IREG_AL);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADD_AX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVZX(ir, IREG_flags_op1, IREG_AX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp0_W, cs + op_pc);
        uop_ADD(ir, IREG_AX, IREG_AX, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
    } else {
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_ADD_IMM(ir, IREG_AX, IREG_AX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 2);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD16);
    uop_MOVZX(ir, IREG_flags_res, IREG_AX);

    codegen_flags_changed = 1;
    return op_pc + 2;
}
uint32_t
ropADD_EAX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOV(ir, IREG_flags_op1, IREG_EAX);

    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp0, cs + op_pc);
        uop_ADD(ir, IREG_EAX, IREG_EAX, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_temp0);
    } else {
        fetchdat = fastreadl(cs + op_pc);
        uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, fetchdat);
        uop_MOV_IMM(ir, IREG_flags_op2, fetchdat);
        codegen_mark_code_present(block, cs + op_pc, 4);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD32);
    uop_MOV(ir, IREG_flags_res, IREG_EAX);
    codegen_flags_changed = 1;
    return op_pc + 4;
}
uint32_t
ropADD_b_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD8);
    uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADD_b_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);

        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_ADD(ir, IREG_temp1_B, IREG_temp0_B, IREG_8(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD8);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADD_w_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD16);
    uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADD_w_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);

        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_ADD(ir, IREG_temp1_W, IREG_temp0_W, IREG_16(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD16);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADD_l_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_flags_op2, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_flags_op2);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD32);
    uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADD_l_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);

        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_ADD(ir, IREG_temp1, IREG_temp0, IREG_32(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_MOV(ir, IREG_flags_res, IREG_temp1);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD32);

    codegen_flags_changed = 1;
    return op_pc + 1;
}

uint32_t
ropCMP_AL_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVZX(ir, IREG_flags_op1, IREG_AL);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0_B, cs + op_pc);
        uop_SUB(ir, IREG_flags_res_B, IREG_AL, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
    } else {
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_SUB_IMM(ir, IREG_flags_res_B, IREG_AL, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 1);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
    uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropCMP_AX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVZX(ir, IREG_flags_op1, IREG_AX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp0_W, cs + op_pc);
        uop_SUB(ir, IREG_flags_res_W, IREG_AX, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
    } else {
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_SUB_IMM(ir, IREG_flags_res_W, IREG_AX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 2);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
    uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);

    codegen_flags_changed = 1;
    return op_pc + 2;
}
uint32_t
ropCMP_EAX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOV(ir, IREG_flags_op1, IREG_EAX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp0, cs + op_pc);
        uop_SUB(ir, IREG_flags_res, IREG_EAX, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_temp0);
    } else {
        uint32_t imm_data = fastreadl(cs + op_pc);

        uop_SUB_IMM(ir, IREG_flags_res, IREG_EAX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 4);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);

    codegen_flags_changed = 1;
    return op_pc + 4;
}
uint32_t
ropCMP_b_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_flags_res_B, IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
        uop_SUB(ir, IREG_flags_res_B, IREG_8(dest_reg), IREG_temp0_B);
    }

    uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropCMP_b_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_flags_res_B, IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_flags_res_B, IREG_temp0_B, IREG_8(src_reg));
    }

    uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropCMP_w_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_flags_res_W, IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
        uop_SUB(ir, IREG_flags_res_W, IREG_16(dest_reg), IREG_temp0_W);
    }

    uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropCMP_w_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_flags_res_W, IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_flags_res_W, IREG_temp0_W, IREG_16(src_reg));
    }

    uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropCMP_l_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_flags_res, IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_temp0);
        uop_SUB(ir, IREG_flags_res, IREG_32(dest_reg), IREG_temp0);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropCMP_l_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_flags_res, IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_flags_res, IREG_temp0, IREG_32(src_reg));
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);

    codegen_flags_changed = 1;
    return op_pc + 1;
}

uint32_t
ropSBB_AL_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    get_cf(ir, IREG_temp0);
    uop_MOVZX(ir, IREG_flags_op1, IREG_AL);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp1_B, cs + op_pc);
        uop_SUB(ir, IREG_AL, IREG_AL, IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp1_B);
    } else {
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_SUB_IMM(ir, IREG_AL, IREG_AL, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 1);
    }
    uop_SUB(ir, IREG_AL, IREG_AL, IREG_temp0_B);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC8);
    uop_MOVZX(ir, IREG_flags_res, IREG_AL);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSBB_AX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    get_cf(ir, IREG_temp0);
    uop_MOVZX(ir, IREG_flags_op1, IREG_AX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp1_W, cs + op_pc);
        uop_SUB(ir, IREG_AX, IREG_AX, IREG_temp1_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp1_W);
    } else {
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_SUB_IMM(ir, IREG_AX, IREG_AX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 2);
    }
    uop_SUB(ir, IREG_AX, IREG_AX, IREG_temp0_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC16);
    uop_MOVZX(ir, IREG_flags_res, IREG_AX);

    codegen_flags_changed = 1;
    return op_pc + 2;
}
uint32_t
ropSBB_EAX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    get_cf(ir, IREG_temp0);
    uop_MOV(ir, IREG_flags_op1, IREG_EAX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp1, cs + op_pc);
        uop_SUB(ir, IREG_EAX, IREG_EAX, IREG_temp1);
        uop_MOV(ir, IREG_flags_op2, IREG_temp1);
    } else {
        uint32_t imm_data = fastreadl(cs + op_pc);

        uop_SUB_IMM(ir, IREG_EAX, IREG_EAX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 4);
    }
    uop_SUB(ir, IREG_EAX, IREG_EAX, IREG_temp0);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC32);
    uop_MOV(ir, IREG_flags_res, IREG_EAX);

    codegen_flags_changed = 1;
    return op_pc + 4;
}
uint32_t
ropSBB_b_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
    }

    uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp1_B);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC8);
    uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSBB_b_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);

        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SUB(ir, IREG_temp2_B, IREG_temp0_B, IREG_8(src_reg));
        uop_SUB(ir, IREG_temp2_B, IREG_temp2_B, IREG_temp1_B);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp2_B);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp2_B);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC8);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSBB_w_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
    }

    uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC16);
    uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSBB_w_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);

        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SUB(ir, IREG_temp2_W, IREG_temp0_W, IREG_16(src_reg));
        uop_SUB(ir, IREG_temp2_W, IREG_temp2_W, IREG_temp1_W);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp2_W);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp2_W);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC16);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSBB_l_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_flags_op2, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_flags_op2);
    }

    uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC32);
    uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSBB_l_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);

        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SUB(ir, IREG_temp2, IREG_temp0, IREG_32(src_reg));
        uop_SUB(ir, IREG_temp2, IREG_temp2, IREG_temp1);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp2);
        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_MOV(ir, IREG_flags_res, IREG_temp2);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC32);

    codegen_flags_changed = 1;
    return op_pc + 1;
}

uint32_t
ropSUB_AL_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVZX(ir, IREG_flags_op1, IREG_AL);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0_B, cs + op_pc);
        uop_SUB(ir, IREG_AL, IREG_AL, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
    } else {
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_SUB_IMM(ir, IREG_AL, IREG_AL, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 1);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
    uop_MOVZX(ir, IREG_flags_res, IREG_AL);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSUB_AX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVZX(ir, IREG_flags_op1, IREG_AX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp0_W, cs + op_pc);
        uop_SUB(ir, IREG_AX, IREG_AX, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
    } else {
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_SUB_IMM(ir, IREG_AX, IREG_AX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 2);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
    uop_MOVZX(ir, IREG_flags_res, IREG_AX);

    codegen_flags_changed = 1;
    return op_pc + 2;
}
uint32_t
ropSUB_EAX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOV(ir, IREG_flags_op1, IREG_EAX);

    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp0, cs + op_pc);
        uop_SUB(ir, IREG_EAX, IREG_EAX, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_temp0);
    } else {
        fetchdat = fastreadl(cs + op_pc);
        codegen_mark_code_present(block, cs + op_pc, 4);
        uop_SUB_IMM(ir, IREG_EAX, IREG_EAX, fetchdat);
        uop_MOV_IMM(ir, IREG_flags_op2, fetchdat);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
    uop_MOV(ir, IREG_flags_res, IREG_EAX);

    codegen_flags_changed = 1;
    return op_pc + 4;
}
uint32_t
ropSUB_b_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
    uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSUB_b_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);

        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SUB(ir, IREG_temp1_B, IREG_temp0_B, IREG_8(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSUB_w_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
    uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSUB_w_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);

        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SUB(ir, IREG_temp1_W, IREG_temp0_W, IREG_16(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSUB_l_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_flags_op2, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_flags_op2);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
    uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSUB_l_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);

        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SUB(ir, IREG_temp1, IREG_temp0, IREG_32(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_MOV(ir, IREG_flags_res, IREG_temp1);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);

    codegen_flags_changed = 1;
    return op_pc + 1;
}

uint32_t
rop80(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int skip_immediate = 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int     dest_reg = fetchdat & 7;
        uint8_t imm      = fastreadb(cs + op_pc + 1);

        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            skip_immediate = 1;
            LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0_B, cs + op_pc + 1);
        }

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
                } else {
                    uop_ADD_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x08: /*OR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_OR(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                else
                    uop_OR_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp1);
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
                } else {
                    uop_ADD_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp1_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp1);
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
                } else {
                    uop_SUB_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp1_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x20: /*AND*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_AND(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                else
                    uop_AND_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x28: /*SUB*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
                } else {
                    uop_SUB_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x30: /*XOR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_XOR(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                else
                    uop_XOR_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x38: /*CMP*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_flags_res_B, IREG_8(dest_reg), IREG_temp0_B);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
                } else {
                    uop_SUB_IMM(ir, IREG_flags_res_B, IREG_8(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
                break;

            default:
                return 0;
        }
    } else {
        x86seg *target_seg;
        uint8_t imm;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if ((fetchdat & 0x38) == 0x38) /*CMP*/
            codegen_check_seg_read(block, ir, target_seg);
        else
            codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        imm = fastreadb(cs + op_pc + 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);

        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            skip_immediate = 1;
            LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp2, cs + op_pc + 1);
        }

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_ADD(ir, IREG_temp1_B, IREG_temp0_B, IREG_temp2_B);
                else
                    uop_ADD_IMM(ir, IREG_temp1_B, IREG_temp0_B, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_B);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            case 0x08: /*OR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_OR(ir, IREG_temp0_B, IREG_temp0_B, IREG_temp2_B);
                else
                    uop_OR_IMM(ir, IREG_temp0_B, IREG_temp0_B, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp3);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_ADD(ir, IREG_temp1_B, IREG_temp0_B, IREG_temp2_B);
                else
                    uop_ADD_IMM(ir, IREG_temp1_B, IREG_temp0_B, imm);
                uop_ADD(ir, IREG_temp1_B, IREG_temp1_B, IREG_temp3_B);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_B);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp3);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_SUB(ir, IREG_temp1_B, IREG_temp0_B, IREG_temp2_B);
                else
                    uop_SUB_IMM(ir, IREG_temp1_B, IREG_temp0_B, imm);
                uop_SUB(ir, IREG_temp1_B, IREG_temp1_B, IREG_temp3_B);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_B);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            case 0x20: /*AND*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_AND(ir, IREG_temp0_B, IREG_temp0_B, IREG_temp2_B);
                else
                    uop_AND_IMM(ir, IREG_temp0_B, IREG_temp0_B, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
                break;

            case 0x28: /*SUB*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_SUB(ir, IREG_temp1_B, IREG_temp0_B, IREG_temp2_B);
                else
                    uop_SUB_IMM(ir, IREG_temp1_B, IREG_temp0_B, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_B);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            case 0x30: /*XOR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_XOR(ir, IREG_temp0_B, IREG_temp0_B, IREG_temp2_B);
                else
                    uop_XOR_IMM(ir, IREG_temp0_B, IREG_temp0_B, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
                break;

            case 0x38: /*CMP*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_B);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_SUB(ir, IREG_flags_res_B, IREG_temp0_B, IREG_temp2_B);
                else
                    uop_SUB_IMM(ir, IREG_flags_res_B, IREG_temp0_B, imm);
                uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    if (!skip_immediate)
        codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}
/*The immediate of 81 or 83 read from RAM as the block runs
  (CODEBLOCK_NO_IMMEDIATES): one of the operand's size, or an imm8 sign
  extended to it. dest_reg is long_reg's view of the operand's size,
  byte_reg its low byte.*/
static void
load_arith_imm(codeblock_t *block, ir_data_t *ir, int dest_reg, int long_reg, int byte_reg, uint32_t addr, int size32, int imm8)
{
    /*Loaded whole: a part of a temporary written alone has its old
      value read in first*/
    if (imm8) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, long_reg, addr);
        uop_MOVSX(ir, dest_reg, byte_reg);
    } else if (size32)
        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, long_reg, addr);
    else
        LOAD_IMMEDIATE_FROM_RAM_16(block, ir, long_reg, addr);
}

static uint32_t
arith_imm_w(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int imm8)
{
    int skip_immediate = 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int      dest_reg = fetchdat & 7;
        uint16_t imm      = imm8 ? (uint16_t) (int16_t) (int8_t) fastreadb(cs + op_pc + 1) : fastreadw(cs + op_pc + 1);

        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            skip_immediate = 1;
            load_arith_imm(block, ir, IREG_temp0_W, IREG_temp0, IREG_temp0_B, cs + op_pc + 1, 0, imm8);
        }

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
                } else {
                    uop_ADD_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x08: /*OR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_OR(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                else
                    uop_OR_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp1);
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
                } else {
                    uop_ADD_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp1);
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
                } else {
                    uop_SUB_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x20: /*AND*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_AND(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                else
                    uop_AND_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x28: /*SUB*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
                } else {
                    uop_SUB_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x30: /*XOR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_XOR(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                else
                    uop_XOR_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x38: /*CMP*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_flags_res_W, IREG_16(dest_reg), IREG_temp0_W);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
                } else {
                    uop_SUB_IMM(ir, IREG_flags_res_W, IREG_16(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
                break;

            default:
                return 0;
        }
    } else {
        x86seg  *target_seg;
        uint16_t imm;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if ((fetchdat & 0x38) == 0x38) /*CMP*/
            codegen_check_seg_read(block, ir, target_seg);
        else
            codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        imm = imm8 ? (uint16_t) (int16_t) (int8_t) fastreadb(cs + op_pc + 1) : fastreadw(cs + op_pc + 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            skip_immediate = 1;
            load_arith_imm(block, ir, IREG_temp2_W, IREG_temp2, IREG_temp2_B, cs + op_pc + 1, 0, imm8);
        }

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_ADD(ir, IREG_temp1_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_ADD_IMM(ir, IREG_temp1_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_W);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x08: /*OR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_OR(ir, IREG_temp0_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_OR_IMM(ir, IREG_temp0_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp3);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_ADD(ir, IREG_temp1_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_ADD_IMM(ir, IREG_temp1_W, IREG_temp0_W, imm);
                uop_ADD(ir, IREG_temp1_W, IREG_temp1_W, IREG_temp3_W);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_W);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp3);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_SUB(ir, IREG_temp1_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_SUB_IMM(ir, IREG_temp1_W, IREG_temp0_W, imm);
                uop_SUB(ir, IREG_temp1_W, IREG_temp1_W, IREG_temp3_W);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_W);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x20: /*AND*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_AND(ir, IREG_temp0_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_AND_IMM(ir, IREG_temp0_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                break;

            case 0x28: /*SUB*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_SUB(ir, IREG_temp1_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_SUB_IMM(ir, IREG_temp1_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_W);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x30: /*XOR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_XOR(ir, IREG_temp0_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_XOR_IMM(ir, IREG_temp0_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                break;

            case 0x38: /*CMP*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_W);
                    uop_SUB(ir, IREG_flags_res_W, IREG_temp0_W, IREG_temp2_W);
                } else {
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                    uop_SUB_IMM(ir, IREG_flags_res_W, IREG_temp0_W, imm);
                }
                uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    if (!skip_immediate)
        codegen_mark_code_present(block, cs + op_pc + 1, imm8 ? 1 : 2);
    return op_pc + (imm8 ? 2 : 3);
}
uint32_t
rop81_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return arith_imm_w(block, ir, fetchdat, op_32, op_pc, 0);
}
static uint32_t
arith_imm_l(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int imm8)
{
    int skip_immediate = 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int      dest_reg = fetchdat & 7;
        uint32_t imm      = imm8 ? (uint32_t) (int32_t) (int8_t) fastreadb(cs + op_pc + 1) : fastreadl(cs + op_pc + 1);

        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            skip_immediate = 1;
            load_arith_imm(block, ir, IREG_temp0, IREG_temp0, IREG_temp0_B, cs + op_pc + 1, 1, imm8);
        }

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                    uop_MOV(ir, IREG_flags_op2, IREG_temp0);
                } else {
                    uop_ADD_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x08: /*OR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_OR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                else
                    uop_OR_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                    uop_MOV(ir, IREG_flags_op2, IREG_temp0);
                } else {
                    uop_ADD_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                    uop_MOV(ir, IREG_flags_op2, IREG_temp0);
                } else {
                    uop_SUB_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x20: /*AND*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_AND(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                else
                    uop_AND_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x28: /*SUB*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                    uop_MOV(ir, IREG_flags_op2, IREG_temp0);
                } else {
                    uop_SUB_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x30: /*XOR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_XOR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                else
                    uop_XOR_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x38: /*CMP*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_flags_res, IREG_32(dest_reg), IREG_temp0);
                    uop_MOV(ir, IREG_flags_op2, IREG_temp0);
                } else {
                    uop_SUB_IMM(ir, IREG_flags_res, IREG_32(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
                break;

            default:
                return 0;
        }
    } else {
        x86seg  *target_seg;
        uint32_t imm;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if ((fetchdat & 0x38) == 0x38) /*CMP*/
            codegen_check_seg_read(block, ir, target_seg);
        else
            codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        imm = imm8 ? (uint32_t) (int32_t) (int8_t) fastreadb(cs + op_pc + 1) : fastreadl(cs + op_pc + 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);

        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            skip_immediate = 1;
            load_arith_imm(block, ir, IREG_temp2, IREG_temp2, IREG_temp2_B, cs + op_pc + 1, 1, imm8);
        }

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_ADD(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                else
                    uop_ADD_IMM(ir, IREG_temp1, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x08: /*OR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp2);
                else
                    uop_OR_IMM(ir, IREG_temp0, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp3);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_ADD(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                else
                    uop_ADD_IMM(ir, IREG_temp1, IREG_temp0, imm);
                uop_ADD(ir, IREG_temp1, IREG_temp1, IREG_temp3);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp3);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_SUB(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                else
                    uop_SUB_IMM(ir, IREG_temp1, IREG_temp0, imm);
                uop_SUB(ir, IREG_temp1, IREG_temp1, IREG_temp3);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x20: /*AND*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_AND(ir, IREG_temp0, IREG_temp0, IREG_temp2);
                else
                    uop_AND_IMM(ir, IREG_temp0, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                break;

            case 0x28: /*SUB*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_SUB(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                else
                    uop_SUB_IMM(ir, IREG_temp1, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x30: /*XOR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_XOR(ir, IREG_temp0, IREG_temp0, IREG_temp2);
                else
                    uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                break;

            case 0x38: /*CMP*/
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                    uop_SUB(ir, IREG_flags_res, IREG_temp0, IREG_temp2);
                } else {
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                    uop_SUB_IMM(ir, IREG_flags_res, IREG_temp0, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    if (!skip_immediate)
        codegen_mark_code_present(block, cs + op_pc + 1, imm8 ? 1 : 4);
    return op_pc + (imm8 ? 2 : 5);
}
uint32_t
rop81_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return arith_imm_l(block, ir, fetchdat, op_32, op_pc, 0);
}

uint32_t
rop83_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return arith_imm_w(block, ir, fetchdat, op_32, op_pc, 1);
}
uint32_t
rop83_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return arith_imm_l(block, ir, fetchdat, op_32, op_pc, 1);
}

static void
rebuild_c(ir_data_t *ir)
{
    int needs_rebuild = 1;

    if (codegen_flags_changed) {
        switch (cpu_state.flags_op) {
            case FLAGS_INC8:
            case FLAGS_INC16:
            case FLAGS_INC32:
            case FLAGS_DEC8:
            case FLAGS_DEC16:
            case FLAGS_DEC32:
                needs_rebuild = 0;
                break;

            default:
                break;
        }
    }

    if (needs_rebuild) {
        uop_CALL_FUNC(ir, jit_flags_rebuild_c);
    }
}

uint32_t
ropINC_r16(UNUSED(UNUSED(codeblock_t *block)), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    rebuild_c(ir);

    uop_MOVZX(ir, IREG_flags_op1, IREG_16(opcode & 7));
    uop_ADD_IMM(ir, IREG_16(opcode & 7), IREG_16(opcode & 7), 1);
    uop_MOVZX(ir, IREG_flags_res, IREG_16(opcode & 7));
    uop_MOV_IMM(ir, IREG_flags_op2, 1);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_INC16);
    codegen_flags_changed = 1;

    return op_pc;
}
uint32_t
ropINC_r32(UNUSED(UNUSED(codeblock_t *block)), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    rebuild_c(ir);

    uop_MOV(ir, IREG_flags_op1, IREG_32(opcode & 7));
    uop_ADD_IMM(ir, IREG_32(opcode & 7), IREG_32(opcode & 7), 1);
    uop_MOV(ir, IREG_flags_res, IREG_32(opcode & 7));
    uop_MOV_IMM(ir, IREG_flags_op2, 1);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_INC32);
    codegen_flags_changed = 1;

    return op_pc;
}

uint32_t
ropDEC_r16(UNUSED(codeblock_t *block), ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    rebuild_c(ir);

    uop_MOVZX(ir, IREG_flags_op1, IREG_16(opcode & 7));
    uop_SUB_IMM(ir, IREG_16(opcode & 7), IREG_16(opcode & 7), 1);
    uop_MOVZX(ir, IREG_flags_res, IREG_16(opcode & 7));
    uop_MOV_IMM(ir, IREG_flags_op2, 1);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_DEC16);
    codegen_flags_changed = 1;

    return op_pc;
}
uint32_t
ropDEC_r32(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    rebuild_c(ir);

    uop_MOV(ir, IREG_flags_op1, IREG_32(opcode & 7));
    uop_SUB_IMM(ir, IREG_32(opcode & 7), IREG_32(opcode & 7), 1);
    uop_MOV(ir, IREG_flags_res, IREG_32(opcode & 7));
    uop_MOV_IMM(ir, IREG_flags_op2, 1);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_DEC32);
    codegen_flags_changed = 1;

    return op_pc;
}

uint32_t
ropINCDEC(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    if (fetchdat & 0x30) /*FE /2-/7 are #UD*/
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    rebuild_c(ir);

    if ((fetchdat & 0xc0) == 0xc0) {
        uop_MOVZX(ir, IREG_flags_op1, IREG_8(fetchdat & 7));
        if (fetchdat & 0x38) {
            uop_SUB_IMM(ir, IREG_8(fetchdat & 7), IREG_8(fetchdat & 7), 1);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_DEC8);
        } else {
            uop_ADD_IMM(ir, IREG_8(fetchdat & 7), IREG_8(fetchdat & 7), 1);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_INC8);
        }
        uop_MOVZX(ir, IREG_flags_res, IREG_8(fetchdat & 7));
        uop_MOV_IMM(ir, IREG_flags_op2, 1);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);

        if (fetchdat & 0x38) {
            uop_SUB_IMM(ir, IREG_temp1_B, IREG_temp0_B, 1);
        } else {
            uop_ADD_IMM(ir, IREG_temp1_B, IREG_temp0_B, 1);
        }
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
        uop_MOV_IMM(ir, IREG_flags_op2, 1);
        if (fetchdat & 0x38) {
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_DEC8);
        } else {
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_INC8);
        }
    }

    return op_pc + 1;
}

/*IMUL r, r/m and IMUL r, r/m, imm. As the interpreter does it: the flags are
  rebuilt, CF and OF say whether the product fit the destination, and the
  other flags keep what they were. imm_len is 0, 1 (sign extended), 2 or 4.*/
static uint32_t
imul_common(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int size32, int imm_len)
{
    int dest_reg       = (fetchdat >> 3) & 7;
    int skip_immediate = 0;
    int src;
    int factor;

#ifndef CODEGEN_BACKEND_HAS_INT_UOPS
    return 0;
#endif
    codegen_mark_code_present(block, cs + op_pc, 1);
    uop_CALL_FUNC(ir, jit_flags_rebuild);

    if ((fetchdat & 0xc0) == 0xc0)
        src = size32 ? IREG_32(fetchdat & 7) : IREG_16(fetchdat & 7);
    else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, size32 ? 3 : 1);
        src = size32 ? IREG_temp0 : IREG_temp0_W;
        uop_MEM_LOAD_REG(ir, src, ireg_seg_base(target_seg), IREG_eaaddr);
    }

    if (imm_len) {
        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            skip_immediate = 1;
            if (imm_len == 1) {
                LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp1_B, cs + op_pc + 1);
                uop_MOVSX(ir, IREG_temp1, IREG_temp1_B);
            } else if (imm_len == 2) {
                LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp1_W, cs + op_pc + 1);
                uop_MOVSX(ir, IREG_temp1, IREG_temp1_W);
            } else
                LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp1, cs + op_pc + 1);
        } else {
            uint32_t imm;

            if (imm_len == 1)
                imm = (int32_t) (int8_t) fastreadb(cs + op_pc + 1);
            else if (imm_len == 2)
                imm = (int32_t) (int16_t) fastreadw(cs + op_pc + 1);
            else
                imm = fastreadl(cs + op_pc + 1);
            uop_MOV_IMM(ir, IREG_temp1, imm);
        }
        factor = size32 ? IREG_temp1 : IREG_temp1_W;
    } else
        factor = size32 ? IREG_32(dest_reg) : IREG_16(dest_reg);

    if (size32) {
        uop_IMUL_OV(ir, IREG_temp2, factor, src);
        uop_IMUL(ir, IREG_32(dest_reg), factor, src);
    } else {
        /*Words sign extended always fit a 32-bit product; it overflows the
          destination if it isn't its own low word sign extended*/
        uop_MOVSX(ir, IREG_temp2, factor);
        uop_MOVSX(ir, IREG_temp3, src);
        uop_IMUL(ir, IREG_temp2, IREG_temp2, IREG_temp3);
        uop_MOV(ir, IREG_16(dest_reg), IREG_temp2_W);
        uop_MOVSX(ir, IREG_temp3, IREG_temp2_W);
        uop_CMP_SET(ir, IREG_temp2, IREG_temp2, IREG_temp3, UOP_CC_NZ);
    }

    /*CF and OF, bits 0 and 11, from the overflow bit in temp2*/
    uop_SHL_IMM(ir, IREG_temp3, IREG_temp2, 11);
    uop_OR(ir, IREG_temp3, IREG_temp3, IREG_temp2);
    uop_AND_IMM(ir, IREG_flags, IREG_flags, (uint16_t) ~(C_FLAG | V_FLAG));
    uop_OR(ir, IREG_flags, IREG_flags, IREG_temp3_W);

    if (imm_len && !skip_immediate)
        codegen_mark_code_present(block, cs + op_pc + 1, imm_len);
    return op_pc + 1 + imm_len;
}

uint32_t
ropIMUL_w_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return imul_common(block, ir, fetchdat, op_32, op_pc, 0, 0);
}
uint32_t
ropIMUL_l_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return imul_common(block, ir, fetchdat, op_32, op_pc, 1, 0);
}
uint32_t
ropIMUL_w_iw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return imul_common(block, ir, fetchdat, op_32, op_pc, 0, 2);
}
uint32_t
ropIMUL_l_il(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return imul_common(block, ir, fetchdat, op_32, op_pc, 1, 4);
}
uint32_t
ropIMUL_w_ib(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return imul_common(block, ir, fetchdat, op_32, op_pc, 0, 1);
}
uint32_t
ropIMUL_l_ib(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return imul_common(block, ir, fetchdat, op_32, op_pc, 1, 1);
}

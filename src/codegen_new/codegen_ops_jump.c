#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_mov.h"

uint32_t
ropJMP_r8(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t offset    = (int32_t) (int8_t) fastreadb(cs + op_pc);
    uint32_t dest_addr = op_pc + 1 + offset;

    if (!(op_32 & 0x100))
        dest_addr &= 0xffff;

    if (offset < 0)
        codegen_can_unroll(block, ir, op_pc + 1, dest_addr);
    codegen_mark_code_present(block, cs + op_pc, 1);
    return dest_addr;
}
uint32_t
ropJMP_r16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint32_t offset    = (int32_t) (int16_t) fastreadw(cs + op_pc);
    uint32_t dest_addr = op_pc + 2 + offset;

    dest_addr &= 0xffff;

    if (offset < 0)
        codegen_can_unroll(block, ir, op_pc + 1, dest_addr);
    codegen_mark_code_present(block, cs + op_pc, 2);
    return dest_addr;
}
uint32_t
ropJMP_r32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint32_t offset    = fastreadl(cs + op_pc);
    uint32_t dest_addr = op_pc + 4 + offset;

    if (offset < 0)
        codegen_can_unroll(block, ir, op_pc + 1, dest_addr);
    codegen_mark_code_present(block, cs + op_pc, 4);
    return dest_addr;
}

uint32_t
ropJMP_far_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t new_pc = fastreadw(cs + op_pc);
    uint16_t new_cs = fastreadw(cs + op_pc + 2);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_pc, new_pc);
    uop_LOAD_FUNC_ARG_IMM(ir, 0, new_cs);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, op_pc + 4);
    uop_CALL_FUNC(ir, loadcsjmp);

    codegen_mark_code_present(block, cs + op_pc, 4);
    return -1;
}
uint32_t
ropJMP_far_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint32_t new_pc = fastreadl(cs + op_pc);
    uint16_t new_cs = fastreadw(cs + op_pc + 4);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_pc, new_pc);
    uop_LOAD_FUNC_ARG_IMM(ir, 0, new_cs);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, op_pc + 4);
    uop_CALL_FUNC(ir, loadcsjmp);

    codegen_mark_code_present(block, cs + op_pc, 6);
    return -1;
}

uint32_t
ropCALL_r16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint32_t offset    = (int32_t) (int16_t) fastreadw(cs + op_pc);
    uint16_t ret_addr  = op_pc + 2;
    uint16_t dest_addr = ret_addr + offset;
    int      sp_reg;

    dest_addr &= 0xffff;

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    sp_reg = LOAD_SP_WITH_OFFSET(ir, -2);
    uop_MEM_STORE_IMM_16(ir, IREG_SS_base, sp_reg, ret_addr);
    SUB_SP(ir, 2);
    uop_MOV_IMM(ir, IREG_pc, dest_addr);

    codegen_mark_code_present(block, cs + op_pc, 2);
    return -1;
}
uint32_t
ropCALL_r32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint32_t offset    = fastreadl(cs + op_pc);
    uint32_t ret_addr  = op_pc + 4;
    uint32_t dest_addr = ret_addr + offset;
    int      sp_reg;

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    sp_reg = LOAD_SP_WITH_OFFSET(ir, -4);
    uop_MEM_STORE_IMM_32(ir, IREG_SS_base, sp_reg, ret_addr);
    SUB_SP(ir, 4);
    uop_MOV_IMM(ir, IREG_pc, dest_addr);

    codegen_mark_code_present(block, cs + op_pc, 4);
    return -1;
}

uint32_t
ropRET_16(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), UNUSED(uint32_t op_pc))
{
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32)
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);
    else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);
    }
    ADD_SP(ir, 2);
    uop_MOVZX(ir, IREG_pc, IREG_temp0_W);

    return -1;
}
uint32_t
ropRET_32(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), UNUSED(uint32_t op_pc))
{
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32)
        uop_MEM_LOAD_REG(ir, IREG_pc, IREG_SS_base, IREG_ESP);
    else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_pc, IREG_SS_base, IREG_eaaddr);
    }
    ADD_SP(ir, 4);

    return -1;
}

uint32_t
ropRET_imm_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t offset = fastreadw(cs + op_pc);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32)
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);
    else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);
    }
    ADD_SP(ir, 2 + offset);
    uop_MOVZX(ir, IREG_pc, IREG_temp0_W);

    codegen_mark_code_present(block, cs + op_pc, 2);
    return -1;
}
uint32_t
ropRET_imm_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t offset = fastreadw(cs + op_pc);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32)
        uop_MEM_LOAD_REG(ir, IREG_pc, IREG_SS_base, IREG_ESP);
    else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_pc, IREG_SS_base, IREG_eaaddr);
    }
    ADD_SP(ir, 4 + offset);

    codegen_mark_code_present(block, cs + op_pc, 2);
    return -1;
}

uint32_t
ropRETF_16(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), UNUSED(uint32_t op_pc))
{
    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG))
        return 0;

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32) {
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_ESP, 2);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_eaaddr, 2);
    }
    uop_MOVZX(ir, IREG_pc, IREG_temp0_W);
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_CALL_FUNC(ir, loadcs);
    ADD_SP(ir, 4);

    return -1;
}
uint32_t
ropRETF_32(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), UNUSED(uint32_t op_pc))
{
    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG))
        return 0;

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32) {
        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_ESP);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_ESP, 4);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_eaaddr);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_eaaddr, 4);
    }
    uop_MOV(ir, IREG_pc, IREG_temp0);
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_CALL_FUNC(ir, loadcs);
    ADD_SP(ir, 8);

    return -1;
}

uint32_t
ropRETF_imm_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t offset;

    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG))
        return 0;

    offset = fastreadw(cs + op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32) {
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_ESP, 2);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_eaaddr, 2);
    }
    uop_MOVZX(ir, IREG_pc, IREG_temp0_W);
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_CALL_FUNC(ir, loadcs);
    ADD_SP(ir, 4 + offset);

    codegen_mark_code_present(block, cs + op_pc, 2);
    return -1;
}
uint32_t
ropRETF_imm_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t offset;

    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG))
        return 0;

    offset = fastreadw(cs + op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32) {
        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_ESP);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_ESP, 4);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_eaaddr);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_eaaddr, 4);
    }
    uop_MOV(ir, IREG_pc, IREG_temp0);
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_CALL_FUNC(ir, loadcs);
    ADD_SP(ir, 8 + offset);

    codegen_mark_code_present(block, cs + op_pc, 2);
    return -1;
}

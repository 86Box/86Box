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
#include "x87_sf.h"
#include "x87.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_fpu_arith.h"
#include "codegen_ops_helpers.h"

uint32_t
ropFLDs(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_read(block, ir, target_seg);
    uop_MEM_LOAD_SINGLE(ir, IREG_ST(-1), ireg_seg_base(target_seg), IREG_eaaddr);
    uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID);
    fpu_PUSH(block, ir);

    return op_pc + 1;
}
uint32_t
ropFLDd(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_read(block, ir, target_seg);
    uop_MEM_LOAD_DOUBLE(ir, IREG_ST(-1), ireg_seg_base(target_seg), IREG_eaaddr);
    uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID);
    fpu_PUSH(block, ir);

    return op_pc + 1;
}

uint32_t
ropFSTs(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_write(block, ir, target_seg);
    uop_MEM_STORE_SINGLE(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_ST(0));

    return op_pc + 1;
}
uint32_t
ropFSTPs(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_write(block, ir, target_seg);
    uop_MEM_STORE_SINGLE(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_ST(0));
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);
    fpu_POP(block, ir);

    return op_pc + 1;
}
uint32_t
ropFSTd(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_write(block, ir, target_seg);
    CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
    uop_MEM_STORE_DOUBLE(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_ST(0));

    return op_pc + 1;
}
uint32_t
ropFSTPd(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_write(block, ir, target_seg);
    CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
    uop_MEM_STORE_DOUBLE(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_ST(0));
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);
    fpu_POP(block, ir);

    return op_pc + 1;
}

uint32_t
ropFILDw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_read(block, ir, target_seg);
    uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
    uop_MOV_DOUBLE_INT(ir, IREG_ST(-1), IREG_temp0_W);
    uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID);
    fpu_PUSH(block, ir);

    return op_pc + 1;
}
uint32_t
ropFILDl(codeblock_t *block, ir_data_t *ir, uint8_t UNUSED(opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_read(block, ir, target_seg);
    uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
    uop_MOV_DOUBLE_INT(ir, IREG_ST(-1), IREG_temp0);
    uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID);
    fpu_PUSH(block, ir);

    return op_pc + 1;
}
uint32_t
ropFILDq(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_read(block, ir, target_seg);
    uop_MEM_LOAD_REG(ir, IREG_ST_i64(-1), ireg_seg_base(target_seg), IREG_eaaddr);
    uop_MOV_DOUBLE_INT(ir, IREG_ST(-1), IREG_ST_i64(-1));
    uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID | TAG_UINT64);
    fpu_PUSH(block, ir);

    return op_pc + 1;
}

uint32_t
ropFISTw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_write(block, ir, target_seg);
    uop_MOV_INT_DOUBLE(ir, IREG_temp0_W, IREG_ST(0));
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);

    return op_pc + 1;
}
uint32_t
ropFISTPw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_write(block, ir, target_seg);
    uop_MOV_INT_DOUBLE(ir, IREG_temp0_W, IREG_ST(0));
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);
    fpu_POP(block, ir);

    return op_pc + 1;
}
uint32_t
ropFISTl(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_write(block, ir, target_seg);
    uop_MOV_INT_DOUBLE(ir, IREG_temp0, IREG_ST(0));
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);

    return op_pc + 1;
}
uint32_t
ropFISTPl(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_write(block, ir, target_seg);
    uop_MOV_INT_DOUBLE(ir, IREG_temp0, IREG_ST(0));
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);
    fpu_POP(block, ir);

    return op_pc + 1;
}
uint32_t
ropFISTPq(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_write(block, ir, target_seg);
    uop_MOV_INT_DOUBLE_64(ir, IREG_temp0_Q, IREG_ST(0), IREG_ST_i64(0), IREG_tag(0));
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_Q);
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);
    fpu_POP(block, ir);

    return op_pc + 1;
}

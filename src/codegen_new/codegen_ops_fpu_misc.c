#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "x86.h"
#include "x86_flags.h"
#include "386_common.h"
#include "x87.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_fpu_misc.h"
#include "codegen_ops_helpers.h"

uint32_t ropFFREE(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_MOV(ir, IREG_tag(dest_reg), TAG_EMPTY);

        return op_pc;
}

uint32_t ropFLD(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_MOV(ir, IREG_ST(-1), IREG_ST(src_reg));
        uop_MOV(ir, IREG_ST_i64(-1), IREG_ST_i64(src_reg));
        uop_MOV(ir, IREG_tag(-1), IREG_tag(src_reg));
        fpu_PUSH(block, ir);

        return op_pc;
}

uint32_t ropFST(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_MOV(ir, IREG_ST(dest_reg), IREG_ST(0));
        uop_MOV(ir, IREG_ST_i64(dest_reg), IREG_ST_i64(0));
        uop_MOV(ir, IREG_tag(dest_reg), IREG_tag(0));

        return op_pc;
}
uint32_t ropFSTP(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_MOV(ir, IREG_ST(dest_reg), IREG_ST(0));
        uop_MOV(ir, IREG_ST_i64(dest_reg), IREG_ST_i64(0));
        uop_MOV(ir, IREG_tag(dest_reg), IREG_tag(0));
        fpu_POP(block, ir);

        return op_pc;
}

uint32_t ropFSTCW(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        x86seg *target_seg;

        uop_FP_ENTER(ir);
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        op_pc--;
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_NPXC);

        return op_pc+1;
}
uint32_t ropFSTSW(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        x86seg *target_seg;

        uop_FP_ENTER(ir);
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        op_pc--;
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_NPXS);

        return op_pc+1;
}
uint32_t ropFSTSW_AX(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_FP_ENTER(ir);
        uop_MOV(ir, IREG_AX, IREG_NPXS);

        return op_pc;
}

uint32_t ropFXCH(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_MOV(ir, IREG_temp0_D, IREG_ST(0));
        uop_MOV(ir, IREG_temp1_Q, IREG_ST_i64(0));
        uop_MOV(ir, IREG_temp2, IREG_tag(0));
        uop_MOV(ir, IREG_ST(0), IREG_ST(dest_reg));
        uop_MOV(ir, IREG_ST_i64(0), IREG_ST_i64(dest_reg));
        uop_MOV(ir, IREG_tag(0), IREG_tag(dest_reg));
        uop_MOV(ir, IREG_ST(dest_reg), IREG_temp0_D);
        uop_MOV(ir, IREG_ST_i64(dest_reg), IREG_temp1_Q);
        uop_MOV(ir, IREG_tag(dest_reg), IREG_temp2);

        return op_pc;
}

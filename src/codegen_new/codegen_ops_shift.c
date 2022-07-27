#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "x86.h"
#include "x86_flags.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_backend.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_shift.h"

static uint32_t shift_common_8(ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, x86seg *target_seg, int count)
{
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                        uop_SHL_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                        break;

                        case 0x28: /*SHR*/
                        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                        uop_SHR_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                        break;

                        case 0x38: /*SAR*/
                        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                        uop_SAR_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                        break;

                        default:
                        return 0;
                }
        }
        else
        {
                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL_IMM(ir, IREG_temp0_B, IREG_temp0_B, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR_IMM(ir, IREG_temp0_B, IREG_temp0_B, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_SHL_IMM(ir, IREG_temp1_B, IREG_temp0_B, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                        break;

                        case 0x28: /*SHR*/
                        uop_SHR_IMM(ir, IREG_temp1_B, IREG_temp0_B, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                        break;

                        case 0x38: /*SAR*/
                        uop_SAR_IMM(ir, IREG_temp1_B, IREG_temp0_B, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                        break;

                        default:
                        return 0;
                }
        }

        codegen_flags_changed = 1;
        return op_pc + 1;
}

static uint32_t shift_common_16(ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, x86seg *target_seg, int count)
{
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                        uop_SHL_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                        break;

                        case 0x28: /*SHR*/
                        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                        uop_SHR_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                        break;

                        case 0x38: /*SAR*/
                        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                        uop_SAR_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                        break;

                        default:
                        return 0;
                }
        }
        else
        {
                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL_IMM(ir, IREG_temp0_W, IREG_temp0_W, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR_IMM(ir, IREG_temp0_W, IREG_temp0_W, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_SHL_IMM(ir, IREG_temp1_W, IREG_temp0_W, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                        break;

                        case 0x28: /*SHR*/
                        uop_SHR_IMM(ir, IREG_temp1_W, IREG_temp0_W, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                        break;

                        case 0x38: /*SAR*/
                        uop_SAR_IMM(ir, IREG_temp1_W, IREG_temp0_W, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                        break;

                        default:
                        return 0;
                }
        }

        codegen_flags_changed = 1;
        return op_pc + 1;
}
static uint32_t shift_common_32(ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, x86seg *target_seg, int count)
{
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                        uop_SHL_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        case 0x28: /*SHR*/
                        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                        uop_SHR_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        case 0x38: /*SAR*/
                        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                        uop_SAR_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), count);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        default:
                        return 0;
                }
        }
        else
        {
                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL_IMM(ir, IREG_temp0, IREG_temp0, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp0);
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR_IMM(ir, IREG_temp0, IREG_temp0, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp0);
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_SHL_IMM(ir, IREG_temp1, IREG_temp0, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp1);
                        break;

                        case 0x28: /*SHR*/
                        uop_SHR_IMM(ir, IREG_temp1, IREG_temp0, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp1);
                        break;

                        case 0x38: /*SAR*/
                        uop_SAR_IMM(ir, IREG_temp1, IREG_temp0, count);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                        uop_MOV_IMM(ir, IREG_flags_op2, count);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp1);
                        break;

                        default:
                        return 0;
                }
        }

        codegen_flags_changed = 1;
        return op_pc + 1;
}

static uint32_t shift_common_variable_32(ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, x86seg *target_seg, int count_reg)
{
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL(ir, IREG_32(dest_reg), IREG_32(dest_reg), count_reg);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR(ir, IREG_32(dest_reg), IREG_32(dest_reg), count_reg);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                        uop_SHL(ir, IREG_32(dest_reg), IREG_32(dest_reg), count_reg);
                        uop_MOV(ir, IREG_flags_op2, count_reg);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        case 0x28: /*SHR*/
                        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                        uop_SHR(ir, IREG_32(dest_reg), IREG_32(dest_reg), count_reg);
                        uop_MOV(ir, IREG_flags_op2, count_reg);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        case 0x38: /*SAR*/
                        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                        uop_SAR(ir, IREG_32(dest_reg), IREG_32(dest_reg), count_reg);
                        uop_MOV(ir, IREG_flags_op2, count_reg);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        default:
                        return 0;
                }
        }
        else
        {
                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL(ir, IREG_temp0, IREG_temp0, count_reg);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp0);
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR(ir, IREG_temp0, IREG_temp0, count_reg);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp0);
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_SHL(ir, IREG_temp1, IREG_temp0, count_reg);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                        uop_MOV(ir, IREG_flags_op2, count_reg);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp1);
                        break;

                        case 0x28: /*SHR*/
                        uop_SHR(ir, IREG_temp1, IREG_temp0, count_reg);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                        uop_MOV(ir, IREG_flags_op2, count_reg);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp1);
                        break;

                        case 0x38: /*SAR*/
                        uop_SAR(ir, IREG_temp1, IREG_temp0, count_reg);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                        uop_MOV(ir, IREG_flags_op2, count_reg);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp1);
                        break;

                        default:
                        return 0;
                }
        }

        codegen_flags_changed = 1;
        return op_pc + 1;
}

uint32_t ropC0(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        x86seg *target_seg = NULL;
        uint8_t imm;

        if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
                return 0;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) != 0xc0)
        {
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        }
        imm = fastreadb(cs + op_pc + 1) & 0x1f;
        codegen_mark_code_present(block, cs+op_pc+1, 1);

        if (imm)
                return shift_common_8(ir, fetchdat, op_pc, target_seg, imm) + 1;
        return op_pc+1;
}
uint32_t ropC1_w(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        x86seg *target_seg = NULL;
        uint8_t imm;

        if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
                return 0;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) != 0xc0)
        {
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        }
        imm = fastreadb(cs + op_pc + 1) & 0x1f;
        codegen_mark_code_present(block, cs+op_pc+1, 1);

        if (imm)
                return shift_common_16(ir, fetchdat, op_pc, target_seg, imm) + 1;
        return op_pc+1;
}
uint32_t ropC1_l(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        x86seg *target_seg = NULL;

        if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
                return 0;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) != 0xc0)
        {
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        }
        if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        {
                uint32_t new_pc;
                int jump_uop;

                LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp2, cs + op_pc + 1);
                uop_AND_IMM(ir, IREG_temp2, IREG_temp2, 0x1f);
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp2, 0);

                new_pc = shift_common_variable_32(ir, fetchdat, op_pc, target_seg, IREG_temp2) + 1;
                uop_NOP_BARRIER(ir);
                uop_set_jump_dest(ir, jump_uop);
                return new_pc;
        }
        else
        {
                uint8_t imm = fastreadb(cs + op_pc + 1) & 0x1f;
                codegen_mark_code_present(block, cs+op_pc+1, 1);

                if (imm)
                        return shift_common_32(ir, fetchdat, op_pc, target_seg, imm) + 1;
        }
        return op_pc+1;
}

uint32_t ropD0(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        x86seg *target_seg = NULL;

        if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
                return 0;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) != 0xc0)
        {
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        }

        return shift_common_8(ir, fetchdat, op_pc, target_seg, 1);
}
uint32_t ropD1_w(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        x86seg *target_seg = NULL;

        if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
                return 0;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) != 0xc0)
        {
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        }

        return shift_common_16(ir, fetchdat, op_pc, target_seg, 1);
}
uint32_t ropD1_l(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        x86seg *target_seg = NULL;

        if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
                return 0;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) != 0xc0)
        {
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        }

        return shift_common_32(ir, fetchdat, op_pc, target_seg, 1);
}

uint32_t ropD2(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
                return 0;

        if (!(CL & 0x1f) || !block->ins)
                return 0;

        uop_AND_IMM(ir, IREG_temp2, REG_ECX, 0x1f);
        uop_CMP_IMM_JZ(ir, IREG_temp2, 0, codegen_exit_rout);

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                        uop_SHL(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp2);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                        break;

                        case 0x28: /*SHR*/
                        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                        uop_SHR(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp2);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                        break;

                        case 0x38: /*SAR*/
                        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                        uop_SAR(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp2);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                        break;

                        default:
                        return 0;
                }
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);

                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL(ir, IREG_temp0_B, IREG_temp0_B, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR(ir, IREG_temp0_B, IREG_temp0_B, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_SHL(ir, IREG_temp1_B, IREG_temp0_B, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                        break;

                        case 0x28: /*SHR*/
                        uop_SHR(ir, IREG_temp1_B, IREG_temp0_B, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                        break;

                        case 0x38: /*SAR*/
                        uop_SAR(ir, IREG_temp1_B, IREG_temp0_B, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR8);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                        break;

                        default:
                        return 0;
                }
        }

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropD3_w(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
                return 0;

        if (!(CL & 0x1f) || !block->ins)
                return 0;

        uop_AND_IMM(ir, IREG_temp2, REG_ECX, 0x1f);
        uop_CMP_IMM_JZ(ir, IREG_temp2, 0, codegen_exit_rout);

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                        uop_SHL(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp2);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                        break;

                        case 0x28: /*SHR*/
                        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                        uop_SHR(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp2);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                        break;

                        case 0x38: /*SAR*/
                        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                        uop_SAR(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp2);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                        break;

                        default:
                        return 0;
                }
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);

                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL(ir, IREG_temp0_W, IREG_temp0_W, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR(ir, IREG_temp0_W, IREG_temp0_W, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_SHL(ir, IREG_temp1_W, IREG_temp0_W, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                        break;

                        case 0x28: /*SHR*/
                        uop_SHR(ir, IREG_temp1_W, IREG_temp0_W, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                        break;

                        case 0x38: /*SAR*/
                        uop_SAR(ir, IREG_temp1_W, IREG_temp0_W, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR16);
                        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                        break;

                        default:
                        return 0;
                }
        }

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropD3_l(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
                return 0;

        if (!(CL & 0x1f) || !block->ins)
                return 0;

        uop_AND_IMM(ir, IREG_temp2, REG_ECX, 0x1f);
        uop_CMP_IMM_JZ(ir, IREG_temp2, 0, codegen_exit_rout);

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                        uop_SHL(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp2);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        case 0x28: /*SHR*/
                        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                        uop_SHR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp2);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        case 0x38: /*SAR*/
                        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                        uop_SAR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp2);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR32);
                        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                        break;

                        default:
                        return 0;
                }
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);

                switch (fetchdat & 0x38)
                {
                        case 0x00: /*ROL*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROL(ir, IREG_temp0, IREG_temp0, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp0);
                        break;

                        case 0x08: /*ROR*/
                        uop_CALL_FUNC(ir, flags_rebuild);
                        uop_ROR(ir, IREG_temp0, IREG_temp0, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp0);
                        break;

                        case 0x20: case 0x30: /*SHL*/
                        uop_SHL(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp1);
                        break;

                        case 0x28: /*SHR*/
                        uop_SHR(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp1);
                        break;

                        case 0x38: /*SAR*/
                        uop_SAR(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                        uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR32);
                        uop_MOV(ir, IREG_flags_res, IREG_temp1);
                        break;

                        default:
                        return 0;
                }
        }

        codegen_flags_changed = 1;
        return op_pc + 1;
}

uint32_t ropSHLD_16_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        x86seg *target_seg = NULL;
        int src_reg = (fetchdat >> 3) & 7;
        uint8_t imm;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) != 0xc0)
        {
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        }
        imm = fastreadb(cs + op_pc + 1) & 0x1f;
        codegen_mark_code_present(block, cs+op_pc+1, 1);

        if (!imm)
                return op_pc+2;

        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                uop_SHL_IMM(ir, IREG_temp0_W, IREG_16(dest_reg), imm);
                uop_SHR_IMM(ir, IREG_temp1_W, IREG_16(src_reg), 16 - imm);
                uop_OR(ir, IREG_16(dest_reg), IREG_temp0_W, IREG_temp1_W);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
        }
        else
        {
                codegen_check_seg_write(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp2_W, ireg_seg_base(target_seg), IREG_eaaddr);

                uop_SHL_IMM(ir, IREG_temp0_W, IREG_temp2, imm);
                uop_SHR_IMM(ir, IREG_temp1_W, IREG_16(src_reg), 16 - imm);
                uop_OR(ir, IREG_temp0_W, IREG_temp0_W, IREG_temp1_W);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);

                uop_MOVZX(ir, IREG_flags_op1, IREG_temp2_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL16);
        }

        return op_pc+2;
}
uint32_t ropSHLD_32_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        x86seg *target_seg = NULL;
        int src_reg = (fetchdat >> 3) & 7;
        uint8_t imm;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) != 0xc0)
        {
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        }
        imm = fastreadb(cs + op_pc + 1) & 0x1f;
        codegen_mark_code_present(block, cs+op_pc+1, 1);

        if (!imm)
                return op_pc+2;

        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SHL_IMM(ir, IREG_temp0, IREG_32(dest_reg), imm);
                uop_SHR_IMM(ir, IREG_temp1, IREG_32(src_reg), 32 - imm);
                uop_OR(ir, IREG_32(dest_reg), IREG_temp0, IREG_temp1);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
        }
        else
        {
                codegen_check_seg_write(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp2, ireg_seg_base(target_seg), IREG_eaaddr);

                uop_SHL_IMM(ir, IREG_temp0, IREG_temp2, imm);
                uop_SHR_IMM(ir, IREG_temp1, IREG_32(src_reg), 32 - imm);
                uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp1);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);

                uop_MOV(ir, IREG_flags_op1, IREG_temp2);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
        }

        return op_pc+2;
}
uint32_t ropSHRD_16_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        x86seg *target_seg = NULL;
        int src_reg = (fetchdat >> 3) & 7;
        uint8_t imm;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) != 0xc0)
        {
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        }
        imm = fastreadb(cs + op_pc + 1) & 0x1f;
        codegen_mark_code_present(block, cs+op_pc+1, 1);

        if (!imm)
                return op_pc+2;

        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                uop_SHR_IMM(ir, IREG_temp0_W, IREG_16(dest_reg), imm);
                uop_SHL_IMM(ir, IREG_temp1_W, IREG_16(src_reg), 16 - imm);
                uop_OR(ir, IREG_16(dest_reg), IREG_temp0_W, IREG_temp1_W);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
        }
        else
        {
                codegen_check_seg_write(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp2_W, ireg_seg_base(target_seg), IREG_eaaddr);

                uop_SHR_IMM(ir, IREG_temp0_W, IREG_temp2, imm);
                uop_SHL_IMM(ir, IREG_temp1_W, IREG_16(src_reg), 16 - imm);
                uop_OR(ir, IREG_temp0_W, IREG_temp0_W, IREG_temp1_W);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);

                uop_MOVZX(ir, IREG_flags_op1, IREG_temp2_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR16);
        }

        return op_pc+2;
}
uint32_t ropSHRD_32_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        x86seg *target_seg = NULL;
        int src_reg = (fetchdat >> 3) & 7;
        uint8_t imm;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) != 0xc0)
        {
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        }
        imm = fastreadb(cs + op_pc + 1) & 0x1f;
        codegen_mark_code_present(block, cs+op_pc+1, 1);

        if (!imm)
                return op_pc+2;

        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SHR_IMM(ir, IREG_temp0, IREG_32(dest_reg), imm);
                uop_SHL_IMM(ir, IREG_temp1, IREG_32(src_reg), 32 - imm);
                uop_OR(ir, IREG_32(dest_reg), IREG_temp0, IREG_temp1);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
        }
        else
        {
                codegen_check_seg_write(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp2, ireg_seg_base(target_seg), IREG_eaaddr);

                uop_SHR_IMM(ir, IREG_temp0, IREG_temp2, imm);
                uop_SHL_IMM(ir, IREG_temp1, IREG_32(src_reg), 32 - imm);
                uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp1);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);

                uop_MOV(ir, IREG_flags_op1, IREG_temp2);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
        }

        return op_pc+2;
}

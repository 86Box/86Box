#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "x86.h"
#include "x86_flags.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_logic.h"

uint32_t ropAND_AL_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_AND_IMM(ir, IREG_AL, IREG_AL, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
        uop_MOVZX(ir, IREG_flags_res, IREG_AL);

        codegen_flags_changed = 1;
        codegen_mark_code_present(block, cs+op_pc, 1);
        return op_pc + 1;
}
uint32_t ropAND_AX_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_AND_IMM(ir, IREG_AX, IREG_AX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
        uop_MOVZX(ir, IREG_flags_res, IREG_AX);

        codegen_flags_changed = 1;
        codegen_mark_code_present(block, cs+op_pc, 2);
        return op_pc + 2;
}
uint32_t ropAND_EAX_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        {
                LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp0, cs + op_pc);
                uop_AND(ir, IREG_EAX, IREG_EAX, IREG_temp0);
        }
        else
        {
                fetchdat = fastreadl(cs + op_pc);
                codegen_mark_code_present(block, cs+op_pc, 4);
                uop_AND_IMM(ir, IREG_EAX, IREG_EAX, fetchdat);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
        uop_MOV(ir, IREG_flags_res, IREG_EAX);

        codegen_flags_changed = 1;
        return op_pc + 4;
}
uint32_t ropAND_b_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int src_reg = fetchdat & 7;
                
                uop_AND(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_read(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_AND(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropAND_b_rmw(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                uop_AND(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);

                uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_AND(ir, IREG_temp0_B, IREG_temp0_B, IREG_8(src_reg));
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropAND_w_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int src_reg = fetchdat & 7;

                uop_AND(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_read(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_AND(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropAND_w_rmw(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                uop_AND(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);

                uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_AND(ir, IREG_temp0_W, IREG_temp0_W, IREG_16(src_reg));
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropAND_l_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int src_reg = fetchdat & 7;

                uop_AND(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_read(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_AND(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropAND_l_rmw(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                uop_AND(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);

                uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_AND(ir, IREG_temp0, IREG_temp0, IREG_32(src_reg));
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);

        codegen_flags_changed = 1;
        return op_pc + 1;
}

uint32_t ropOR_AL_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_OR_IMM(ir, IREG_AL, IREG_AL, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
        uop_MOVZX(ir, IREG_flags_res, IREG_AL);

        codegen_flags_changed = 1;
        codegen_mark_code_present(block, cs+op_pc, 1);
        return op_pc + 1;
}
uint32_t ropOR_AX_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_OR_IMM(ir, IREG_AX, IREG_AX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
        uop_MOVZX(ir, IREG_flags_res, IREG_AX);

        codegen_flags_changed = 1;
        codegen_mark_code_present(block, cs+op_pc, 2);
        return op_pc + 2;
}
uint32_t ropOR_EAX_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        {
                LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp0, cs + op_pc);
                uop_OR(ir, IREG_EAX, IREG_EAX, IREG_temp0);
        }
        else
        {
                fetchdat = fastreadl(cs + op_pc);
                codegen_mark_code_present(block, cs+op_pc, 4);
                uop_OR_IMM(ir, IREG_EAX, IREG_EAX, fetchdat);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
        uop_MOV(ir, IREG_flags_res, IREG_EAX);

        codegen_flags_changed = 1;
        codegen_mark_code_present(block, cs+op_pc, 4);
        return op_pc + 4;
}
uint32_t ropOR_b_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int src_reg = fetchdat & 7;

                uop_OR(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_read(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_OR(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropOR_b_rmw(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                uop_OR(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);

                uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_OR(ir, IREG_temp0_B, IREG_temp0_B, IREG_8(src_reg));
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropOR_w_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int src_reg = fetchdat & 7;

                uop_OR(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_read(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_OR(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropOR_w_rmw(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                uop_OR(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);

                uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_OR(ir, IREG_temp0_W, IREG_temp0_W, IREG_16(src_reg));
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropOR_l_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int src_reg = fetchdat & 7;

                uop_OR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_read(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_OR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
        uop_MOV(ir, IREG_flags_res, dest_reg);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropOR_l_rmw(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                uop_OR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);

                uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_OR(ir, IREG_temp0, IREG_temp0, IREG_32(src_reg));
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);

        codegen_flags_changed = 1;
        return op_pc + 1;
}

uint32_t ropTEST_AL_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_AND_IMM(ir, IREG_flags_res, IREG_EAX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);

        codegen_flags_changed = 1;
        codegen_mark_code_present(block, cs+op_pc, 1);
        return op_pc + 1;
}
uint32_t ropTEST_AX_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_AND_IMM(ir, IREG_flags_res, IREG_EAX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);

        codegen_flags_changed = 1;
        codegen_mark_code_present(block, cs+op_pc, 2);
        return op_pc + 2;
}
uint32_t ropTEST_EAX_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        {
                LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp0, cs + op_pc);
                uop_AND(ir, IREG_flags_res, IREG_EAX, IREG_temp0);
        }
        else
        {
                fetchdat = fastreadl(cs + op_pc);
                codegen_mark_code_present(block, cs+op_pc, 4);
                uop_AND_IMM(ir, IREG_flags_res, IREG_EAX, fetchdat);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);

        codegen_flags_changed = 1;
        codegen_mark_code_present(block, cs+op_pc, 4);
        return op_pc + 4;
}
uint32_t ropTEST_b_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int src_reg = fetchdat & 7;

                uop_AND(ir, IREG_flags_res_B, IREG_8(dest_reg), IREG_8(src_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_read(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_AND(ir, IREG_flags_res_B, IREG_8(dest_reg), IREG_temp0_B);
        }

        uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropTEST_w_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int src_reg = fetchdat & 7;

                uop_AND(ir, IREG_flags_res_W, IREG_16(dest_reg), IREG_16(src_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_read(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_AND(ir, IREG_flags_res_W, IREG_16(dest_reg), IREG_temp0_W);
        }

        uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropTEST_l_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int src_reg = fetchdat & 7;

                uop_AND(ir, IREG_flags_res, IREG_32(dest_reg), IREG_32(src_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_read(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_AND(ir, IREG_flags_res, IREG_32(dest_reg), IREG_temp0);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);

        codegen_flags_changed = 1;
        return op_pc + 1;
}

uint32_t ropXOR_AL_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_XOR_IMM(ir, IREG_AL, IREG_AL, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
        uop_MOVZX(ir, IREG_flags_res, IREG_AL);

        codegen_flags_changed = 1;
        codegen_mark_code_present(block, cs+op_pc, 1);
        return op_pc + 1;
}
uint32_t ropXOR_AX_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_XOR_IMM(ir, IREG_AX, IREG_AX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
        uop_MOVZX(ir, IREG_flags_res, IREG_AX);

        codegen_flags_changed = 1;
        codegen_mark_code_present(block, cs+op_pc, 2);
        return op_pc + 2;
}
uint32_t ropXOR_EAX_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        {
                LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp0, cs + op_pc);
                uop_XOR(ir, IREG_EAX, IREG_EAX, IREG_temp0);
        }
        else
        {
                fetchdat = fastreadl(cs + op_pc);
                codegen_mark_code_present(block, cs+op_pc, 4);
                uop_XOR_IMM(ir, IREG_EAX, IREG_EAX, fetchdat);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
        uop_MOV(ir, IREG_flags_res, IREG_EAX);

        codegen_flags_changed = 1;
        codegen_mark_code_present(block, cs+op_pc, 4);
        return op_pc + 4;
}
uint32_t ropXOR_b_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int src_reg = fetchdat & 7;

                uop_XOR(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_read(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_XOR(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropXOR_b_rmw(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                uop_XOR(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);

                uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_XOR(ir, IREG_temp0_B, IREG_temp0_B, IREG_8(src_reg));
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropXOR_w_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int src_reg = fetchdat & 7;

                uop_XOR(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_read(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_XOR(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropXOR_w_rmw(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                uop_XOR(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);

                uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_XOR(ir, IREG_temp0_W, IREG_temp0_W, IREG_16(src_reg));
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropXOR_l_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int src_reg = fetchdat & 7;

                uop_XOR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_read(block, ir, target_seg);
                uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_XOR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
        uop_MOV(ir, IREG_flags_res, dest_reg);

        codegen_flags_changed = 1;
        return op_pc + 1;
}
uint32_t ropXOR_l_rmw(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = (fetchdat >> 3) & 7;

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                int dest_reg = fetchdat & 7;

                uop_XOR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
        }
        else
        {
                x86seg *target_seg;

                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
                codegen_check_seg_write(block, ir, target_seg);

                uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
                uop_XOR(ir, IREG_temp0, IREG_temp0, IREG_32(src_reg));
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
        }

        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);

        codegen_flags_changed = 1;
        return op_pc + 1;
}

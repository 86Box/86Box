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
#include "codegen_ops_misc.h"

uint32_t ropPUSH_r16(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int sp_reg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -2);
        uop_MEM_STORE_REG(ir, IREG_SS_base, sp_reg, IREG_16(opcode & 7));
        SUB_SP(ir, 2);

        return op_pc;
}
uint32_t ropPUSH_r32(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int sp_reg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -4);
        uop_MEM_STORE_REG(ir, IREG_SS_base, sp_reg, IREG_32(opcode & 7));
        SUB_SP(ir, 4);

        return op_pc;
}

uint32_t ropPOP_r16(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

        if (stack32)
                uop_MEM_LOAD_REG(ir, IREG_16(opcode & 7), IREG_SS_base, IREG_ESP);
        else
        {
                uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
                uop_MEM_LOAD_REG(ir, IREG_16(opcode & 7), IREG_SS_base, IREG_eaaddr);
        }
        if ((opcode & 7) != REG_SP)
                ADD_SP(ir, 2);

        return op_pc;
}
uint32_t ropPOP_r32(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

        if (stack32)
                uop_MEM_LOAD_REG(ir, IREG_32(opcode & 7), IREG_SS_base, IREG_ESP);
        else
        {
                uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
                uop_MEM_LOAD_REG(ir, IREG_32(opcode & 7), IREG_SS_base, IREG_eaaddr);
        }
        if ((opcode & 7) != REG_ESP)
                ADD_SP(ir, 4);

        return op_pc;
}

uint32_t ropPUSH_imm_16(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint16_t imm = fastreadw(cs + op_pc);
        int sp_reg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -2);
        uop_MEM_STORE_IMM_16(ir, IREG_SS_base, sp_reg, imm);
        SUB_SP(ir, 2);

        codegen_mark_code_present(block, cs+op_pc, 2);
        return op_pc + 2;
}
uint32_t ropPUSH_imm_32(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint32_t imm = fastreadl(cs + op_pc);
        int sp_reg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -4);
        uop_MEM_STORE_IMM_32(ir, IREG_SS_base, sp_reg, imm);
        SUB_SP(ir, 4);

        codegen_mark_code_present(block, cs+op_pc, 4);
        return op_pc + 4;
}

uint32_t ropPUSH_imm_16_8(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint16_t imm = (int16_t)(int8_t)fastreadb(cs + op_pc);
        int sp_reg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -2);
        uop_MEM_STORE_IMM_16(ir, IREG_SS_base, sp_reg, imm);
        SUB_SP(ir, 2);

        codegen_mark_code_present(block, cs+op_pc, 1);
        return op_pc + 1;
}
uint32_t ropPUSH_imm_32_8(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint32_t imm = (int32_t)(int8_t)fastreadb(cs + op_pc);
        int sp_reg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -4);
        uop_MEM_STORE_IMM_32(ir, IREG_SS_base, sp_reg, imm);
        SUB_SP(ir, 4);

        codegen_mark_code_present(block, cs+op_pc, 1);
        return op_pc + 1;
}

uint32_t ropPOP_W(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                if (stack32)
                        uop_MEM_LOAD_REG(ir, IREG_16(fetchdat & 7), IREG_SS_base, IREG_ESP);
                else
                {
                        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
                        uop_MEM_LOAD_REG(ir, IREG_16(fetchdat & 7), IREG_SS_base, IREG_eaaddr);
                }
        }
        else
        {
                x86seg *target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 2);
                codegen_check_seg_write(block, ir, target_seg);

                if (stack32)
                        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);
                else
                {
                        uop_MOVZX(ir, IREG_temp0, IREG_SP);
                        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_temp0);
                }

                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
        }

        if ((fetchdat & 0xc7) != (0xc0 | REG_SP))
                ADD_SP(ir, 2);

        return op_pc + 1;
}
uint32_t ropPOP_L(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

        codegen_mark_code_present(block, cs+op_pc, 1);
        if ((fetchdat & 0xc0) == 0xc0)
        {
                if (stack32)
                        uop_MEM_LOAD_REG(ir, IREG_32(fetchdat & 7), IREG_SS_base, IREG_ESP);
                else
                {
                        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
                        uop_MEM_LOAD_REG(ir, IREG_32(fetchdat & 7), IREG_SS_base, IREG_eaaddr);
                }
        }
        else
        {
                x86seg *target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 4);
                codegen_check_seg_write(block, ir, target_seg);

                if (stack32)
                        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_ESP);
                else
                {
                        uop_MOVZX(ir, IREG_temp0, IREG_SP);
                        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_temp0);
                }

                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
        }

        if ((fetchdat & 0xc7) != (0xc0 | REG_ESP))
                ADD_SP(ir, 4);

        return op_pc + 1;
}

#define ROP_PUSH_SEG(seg) \
uint32_t ropPUSH_ ## seg ## _16(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)   \
{                                                                                                                                       \
        int sp_reg;                                                                                                                     \
                                                                                                                                        \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                                   \
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -2);                                                                                           \
        uop_MEM_STORE_REG(ir, IREG_SS_base, sp_reg, IREG_ ## seg ## _seg_W);                                                            \
        SUB_SP(ir, 2);                                                                                                                  \
                                                                                                                                        \
        return op_pc;                                                                                                                   \
}                                                                                                                                       \
uint32_t ropPUSH_ ## seg ## _32(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)   \
{                                                                                                                                       \
        int sp_reg;                                                                                                                     \
                                                                                                                                        \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                                   \
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -4);                                                                                           \
        uop_MOVZX(ir, IREG_temp0, IREG_ ## seg ## _seg_W);                                                                              \
        uop_MEM_STORE_REG(ir, IREG_SS_base, sp_reg, IREG_temp0);                                                                        \
        SUB_SP(ir, 4);                                                                                                                  \
                                                                                                                                        \
        return op_pc;                                                                                                                   \
}

#define ROP_POP_SEG(seg, rseg) \
uint32_t ropPOP_ ## seg ## _16(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)    \
{                                                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                                   \
                                                                                                                                        \
        if (stack32)                                                                                                                    \
                uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);                                                             \
        else                                                                                                                            \
        {                                                                                                                               \
                uop_MOVZX(ir, IREG_eaaddr, IREG_SP);                                                                                    \
                uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);                                                          \
        }                                                                                                                               \
        uop_LOAD_SEG(ir, &rseg, IREG_temp0_W);                                                                                          \
        ADD_SP(ir, 2);                                                                                                                  \
                                                                                                                                        \
        return op_pc;                                                                                                                   \
}                                                                                                                                       \
uint32_t ropPOP_ ## seg ## _32(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)    \
{                                                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                                   \
                                                                                                                                        \
        if (stack32)                                                                                                                    \
                uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);                                                             \
        else                                                                                                                            \
        {                                                                                                                               \
                uop_MOVZX(ir, IREG_eaaddr, IREG_SP);                                                                                    \
                uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);                                                          \
        }                                                                                                                               \
        uop_LOAD_SEG(ir, &rseg, IREG_temp0_W);                                                                                          \
        ADD_SP(ir, 4);                                                                                                                  \
                                                                                                                                        \
        return op_pc;                                                                                                                   \
}


ROP_PUSH_SEG(CS)
ROP_PUSH_SEG(DS)
ROP_PUSH_SEG(ES)
ROP_PUSH_SEG(FS)
ROP_PUSH_SEG(GS)
ROP_PUSH_SEG(SS)
ROP_POP_SEG(DS, cpu_state.seg_ds)
ROP_POP_SEG(ES, cpu_state.seg_es)
ROP_POP_SEG(FS, cpu_state.seg_fs)
ROP_POP_SEG(GS, cpu_state.seg_gs)

uint32_t ropLEAVE_16(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

        if (stack32)
                uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_EBP);
        else
        {
                uop_MOVZX(ir, IREG_eaaddr, IREG_BP);
                uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);
        }
        uop_ADD_IMM(ir, IREG_SP, IREG_BP, 2);
        uop_MOV(ir, IREG_BP, IREG_temp0_W);

        return op_pc;
}
uint32_t ropLEAVE_32(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

        if (stack32)
                uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_EBP);
        else
        {
                uop_MOVZX(ir, IREG_eaaddr, IREG_BP);
                uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_eaaddr);
        }
        uop_ADD_IMM(ir, IREG_ESP, IREG_EBP, 4);
        uop_MOV(ir, IREG_EBP, IREG_temp0);

        return op_pc;
}


uint32_t ropPUSHA_16(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int sp_reg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -16);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg, 14, IREG_AX);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg, 12, IREG_CX);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg, 10, IREG_DX);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg,  8, IREG_BX);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg,  6, IREG_SP);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg,  4, IREG_BP);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg,  2, IREG_SI);
        uop_MEM_STORE_REG(ir, IREG_SS_base, sp_reg, IREG_DI);
        SUB_SP(ir, 16);

        return op_pc;
}
uint32_t ropPUSHA_32(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int sp_reg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -32);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg, 28, IREG_EAX);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg, 24, IREG_ECX);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg, 20, IREG_EDX);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg, 16, IREG_EBX);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg, 12, IREG_ESP);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg,  8, IREG_EBP);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg,  4, IREG_ESI);
        uop_MEM_STORE_REG(ir, IREG_SS_base, sp_reg, IREG_EDI);
        SUB_SP(ir, 32);

        return op_pc;
}

uint32_t ropPOPA_16(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int sp_reg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        sp_reg = LOAD_SP(ir);
        uop_MEM_LOAD_REG(ir, IREG_DI, IREG_SS_base, sp_reg);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_SI, IREG_SS_base, sp_reg,  2);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_BP, IREG_SS_base, sp_reg,  4);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_BX, IREG_SS_base, sp_reg,  8);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_DX, IREG_SS_base, sp_reg, 10);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_CX, IREG_SS_base, sp_reg, 12);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_AX, IREG_SS_base, sp_reg, 14);
        ADD_SP(ir, 16);

        return op_pc;
}
uint32_t ropPOPA_32(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int sp_reg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        sp_reg = LOAD_SP(ir);
        uop_MEM_LOAD_REG(ir, IREG_EDI, IREG_SS_base, sp_reg);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_ESI, IREG_SS_base, sp_reg,  4);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_EBP, IREG_SS_base, sp_reg,  8);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_EBX, IREG_SS_base, sp_reg, 16);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_EDX, IREG_SS_base, sp_reg, 20);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_ECX, IREG_SS_base, sp_reg, 24);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_EAX, IREG_SS_base, sp_reg, 28);
        ADD_SP(ir, 32);

        return op_pc;
}

uint32_t ropPUSHF(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int sp_reg;

        if ((cpu_state.eflags & VM_FLAG) && (IOPL < 3))
                return 0;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        uop_CALL_FUNC(ir, flags_rebuild);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -2);
        uop_MEM_STORE_REG(ir, IREG_SS_base, sp_reg, IREG_flags);
        SUB_SP(ir, 2);

        return op_pc;
}
uint32_t ropPUSHFD(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int sp_reg;

        if ((cpu_state.eflags & VM_FLAG) && (IOPL < 3))
                return 0;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        uop_CALL_FUNC(ir, flags_rebuild);

        if (cpu_CR4_mask & CR4_VME)
                uop_AND_IMM(ir, IREG_temp0_W, IREG_eflags, 0x3c);
        else if (CPUID)
                uop_AND_IMM(ir, IREG_temp0_W, IREG_eflags, 0x24);
        else
                uop_AND_IMM(ir, IREG_temp0_W, IREG_eflags, 4);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -4);
        uop_MEM_STORE_REG(ir, IREG_SS_base, sp_reg, IREG_flags);
        uop_MEM_STORE_REG_OFFSET(ir, IREG_SS_base, sp_reg, 2, IREG_temp0_W);
        SUB_SP(ir, 4);

        return op_pc;
}

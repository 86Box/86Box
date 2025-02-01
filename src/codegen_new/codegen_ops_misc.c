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
#include "codegen_ops_helpers.h"
#include "codegen_ops_misc.h"

uint32_t
ropLEA_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if ((fetchdat & 0xc0) == 0xc0)
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    uop_MOV(ir, IREG_16(dest_reg), IREG_eaaddr_W);

    return op_pc + 1;
}
uint32_t
ropLEA_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if ((fetchdat & 0xc0) == 0xc0)
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    uop_MOV(ir, IREG_32(dest_reg), IREG_eaaddr);

    return op_pc + 1;
}

uint32_t
ropF6(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;
    uint8_t imm_data;
    int     reg;

    if (fetchdat & 0x20)
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0)
        reg = IREG_8(fetchdat & 7);
    else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if ((fetchdat & 0x30) == 0x10) /*NEG/NOT*/
            codegen_check_seg_write(block, ir, target_seg);
        else
            codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        reg = IREG_temp0_B;
    }

    switch (fetchdat & 0x38) {
        case 0x00:
        case 0x08: /*TEST*/
            imm_data = fastreadb(cs + op_pc + 1);

            uop_AND_IMM(ir, IREG_flags_res_B, reg, imm_data);
            uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);

            codegen_flags_changed = 1;
            codegen_mark_code_present(block, cs + op_pc + 1, 1);
            return op_pc + 2;

        case 0x10: /*NOT*/
            uop_XOR_IMM(ir, reg, reg, 0xff);
            if ((fetchdat & 0xc0) != 0xc0)
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, reg);

            codegen_flags_changed = 1;
            return op_pc + 1;

        case 0x18: /*NEG*/
            uop_MOV_IMM(ir, IREG_temp1_B, 0);

            if ((fetchdat & 0xc0) == 0xc0) {
                uop_MOVZX(ir, IREG_flags_op2, reg);
                uop_SUB(ir, IREG_temp1_B, IREG_temp1_B, reg);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                uop_MOV(ir, reg, IREG_temp1_B);
            } else {
                uop_SUB(ir, IREG_temp1_B, IREG_temp1_B, reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
            }
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
            uop_MOV_IMM(ir, IREG_flags_op1, 0);

            codegen_flags_changed = 1;
            return op_pc + 1;

        default:
            break;
    }
    return 0;
}
uint32_t
ropF7_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg  *target_seg = NULL;
    uint16_t imm_data;
    int      reg;

    if (fetchdat & 0x20)
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0)
        reg = IREG_16(fetchdat & 7);
    else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if ((fetchdat & 0x30) == 0x10) /*NEG/NOT*/
            codegen_check_seg_write(block, ir, target_seg);
        else
            codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        reg = IREG_temp0_W;
    }

    switch (fetchdat & 0x38) {
        case 0x00:
        case 0x08: /*TEST*/
            imm_data = fastreadw(cs + op_pc + 1);

            uop_AND_IMM(ir, IREG_flags_res_W, reg, imm_data);
            uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);

            codegen_flags_changed = 1;
            codegen_mark_code_present(block, cs + op_pc + 1, 2);
            return op_pc + 3;

        case 0x10: /*NOT*/
            uop_XOR_IMM(ir, reg, reg, 0xffff);
            if ((fetchdat & 0xc0) != 0xc0)
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, reg);

            codegen_flags_changed = 1;
            return op_pc + 1;

        case 0x18: /*NEG*/
            uop_MOV_IMM(ir, IREG_temp1_W, 0);

            if ((fetchdat & 0xc0) == 0xc0) {
                uop_MOVZX(ir, IREG_flags_op2, reg);
                uop_SUB(ir, IREG_temp1_W, IREG_temp1_W, reg);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                uop_MOV(ir, reg, IREG_temp1_W);
            } else {
                uop_SUB(ir, IREG_temp1_W, IREG_temp1_W, reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
            }
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
            uop_MOV_IMM(ir, IREG_flags_op1, 0);

            codegen_flags_changed = 1;
            return op_pc + 1;

        default:
            break;
    }
    return 0;
}
uint32_t
ropF7_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg  *target_seg = NULL;
    uint32_t imm_data;
    int      reg;

    if (fetchdat & 0x20)
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0)
        reg = IREG_32(fetchdat & 7);
    else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if ((fetchdat & 0x30) == 0x10) /*NEG/NOT*/
            codegen_check_seg_write(block, ir, target_seg);
        else
            codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        reg = IREG_temp0;
    }

    switch (fetchdat & 0x38) {
        case 0x00:
        case 0x08: /*TEST*/
            imm_data = fastreadl(cs + op_pc + 1);

            uop_AND_IMM(ir, IREG_flags_res, reg, imm_data);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);

            codegen_flags_changed = 1;
            codegen_mark_code_present(block, cs + op_pc + 1, 4);
            return op_pc + 5;

        case 0x10: /*NOT*/
            uop_XOR_IMM(ir, reg, reg, 0xffffffff);
            if ((fetchdat & 0xc0) != 0xc0)
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, reg);

            codegen_flags_changed = 1;
            return op_pc + 1;

        case 0x18: /*NEG*/
            uop_MOV_IMM(ir, IREG_temp1, 0);

            if ((fetchdat & 0xc0) == 0xc0) {
                uop_MOV(ir, IREG_flags_op2, reg);
                uop_SUB(ir, IREG_temp1, IREG_temp1, reg);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                uop_MOV(ir, reg, IREG_temp1);
            } else {
                uop_SUB(ir, IREG_temp1, IREG_temp1, reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op2, IREG_temp0);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
            }
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
            uop_MOV_IMM(ir, IREG_flags_op1, 0);

            codegen_flags_changed = 1;
            return op_pc + 1;

        default:
            break;
    }
    return 0;
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
        uop_CALL_FUNC(ir, flags_rebuild_c);
    }
}

uint32_t
ropFF_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;
    int     src_reg;
    int     sp_reg;

    if ((fetchdat & 0x38) != 0x00 && (fetchdat & 0x38) != 0x08 && (fetchdat & 0x38) != 0x10 && (fetchdat & 0x38) != 0x20 && (fetchdat & 0x38) != 0x28 && (fetchdat & 0x38) != 0x30)
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        if ((fetchdat & 0x38) == 0x28)
            return 0;
        src_reg = IREG_16(fetchdat & 7);
    } else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if (!(fetchdat & 0x30)) /*INC/DEC*/
            codegen_check_seg_write(block, ir, target_seg);
        else
            codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        src_reg = IREG_temp0_W;
    }

    switch (fetchdat & 0x38) {
        case 0x00: /*INC*/
            rebuild_c(ir);
            codegen_flags_changed = 1;

            if ((fetchdat & 0xc0) == 0xc0) {
                uop_MOVZX(ir, IREG_flags_op1, src_reg);
                uop_ADD_IMM(ir, src_reg, src_reg, 1);
                uop_MOVZX(ir, IREG_flags_res, src_reg);
                uop_MOV_IMM(ir, IREG_flags_op2, 1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_INC16);
            } else {
                uop_ADD_IMM(ir, IREG_temp1_W, src_reg, 1);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, src_reg);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                uop_MOV_IMM(ir, IREG_flags_op2, 1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_INC16);
            }
            return op_pc + 1;

        case 0x08: /*DEC*/
            rebuild_c(ir);
            codegen_flags_changed = 1;

            if ((fetchdat & 0xc0) == 0xc0) {
                uop_MOVZX(ir, IREG_flags_op1, src_reg);
                uop_SUB_IMM(ir, src_reg, src_reg, 1);
                uop_MOVZX(ir, IREG_flags_res, src_reg);
                uop_MOV_IMM(ir, IREG_flags_op2, 1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_DEC16);
            } else {
                uop_SUB_IMM(ir, IREG_temp1_W, src_reg, 1);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, src_reg);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                uop_MOV_IMM(ir, IREG_flags_op2, 1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_DEC16);
            }
            return op_pc + 1;

        case 0x10: /*CALL*/
            if ((fetchdat & 0xc0) == 0xc0)
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
            sp_reg = LOAD_SP_WITH_OFFSET(ir, -2);
            uop_MEM_STORE_IMM_16(ir, IREG_SS_base, sp_reg, op_pc + 1);
            SUB_SP(ir, 2);
            uop_MOVZX(ir, IREG_pc, src_reg);
            return -1;

        case 0x20: /*JMP*/
            uop_MOVZX(ir, IREG_pc, src_reg);
            return -1;

        case 0x28: /*JMP far*/
            uop_MOVZX(ir, IREG_pc, src_reg);
            uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, ireg_seg_base(target_seg), IREG_eaaddr, 2);
            uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
            uop_LOAD_FUNC_ARG_IMM(ir, 1, op_pc + 1);
            uop_CALL_FUNC(ir, loadcsjmp);
            return -1;

        case 0x30: /*PUSH*/
            if ((fetchdat & 0xc0) == 0xc0)
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
            sp_reg = LOAD_SP_WITH_OFFSET(ir, -2);
            uop_MEM_STORE_REG(ir, IREG_SS_base, sp_reg, src_reg);
            SUB_SP(ir, 2);
            return op_pc + 1;

        default:
            break;
    }
    return 0;
}

uint32_t
ropFF_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;
    int     src_reg;
    int     sp_reg;

    if ((fetchdat & 0x38) != 0x00 && (fetchdat & 0x38) != 0x08 && (fetchdat & 0x38) != 0x10 && (fetchdat & 0x38) != 0x20 && (fetchdat & 0x38) != 0x28 && (fetchdat & 0x38) != 0x30)
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        if ((fetchdat & 0x38) == 0x28)
            return 0;
        src_reg = IREG_32(fetchdat & 7);
    } else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if (!(fetchdat & 0x30)) /*INC/DEC*/
            codegen_check_seg_write(block, ir, target_seg);
        else
            codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        src_reg = IREG_temp0;
    }

    switch (fetchdat & 0x38) {
        case 0x00: /*INC*/
            rebuild_c(ir);
            codegen_flags_changed = 1;

            if ((fetchdat & 0xc0) == 0xc0) {
                uop_MOV(ir, IREG_flags_op1, src_reg);
                uop_ADD_IMM(ir, src_reg, src_reg, 1);
                uop_MOV(ir, IREG_flags_res, src_reg);
                uop_MOV_IMM(ir, IREG_flags_op2, 1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_INC32);
            } else {
                uop_ADD_IMM(ir, IREG_temp1, src_reg, 1);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, src_reg);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                uop_MOV_IMM(ir, IREG_flags_op2, 1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_INC32);
            }
            return op_pc + 1;

        case 0x08: /*DEC*/
            rebuild_c(ir);
            codegen_flags_changed = 1;

            if ((fetchdat & 0xc0) == 0xc0) {
                uop_MOV(ir, IREG_flags_op1, src_reg);
                uop_SUB_IMM(ir, src_reg, src_reg, 1);
                uop_MOV(ir, IREG_flags_res, src_reg);
                uop_MOV_IMM(ir, IREG_flags_op2, 1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_DEC32);
            } else {
                uop_SUB_IMM(ir, IREG_temp1, src_reg, 1);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, src_reg);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                uop_MOV_IMM(ir, IREG_flags_op2, 1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_DEC32);
            }
            return op_pc + 1;

        case 0x10: /*CALL*/
            if ((fetchdat & 0xc0) == 0xc0)
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
            sp_reg = LOAD_SP_WITH_OFFSET(ir, -4);
            uop_MEM_STORE_IMM_32(ir, IREG_SS_base, sp_reg, op_pc + 1);
            SUB_SP(ir, 4);
            uop_MOV(ir, IREG_pc, src_reg);
            return -1;

        case 0x20: /*JMP*/
            uop_MOV(ir, IREG_pc, src_reg);
            return -1;

        case 0x28: /*JMP far*/
            uop_MOV(ir, IREG_pc, src_reg);
            uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, ireg_seg_base(target_seg), IREG_eaaddr, 4);
            uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
            uop_LOAD_FUNC_ARG_IMM(ir, 1, op_pc + 1);
            uop_CALL_FUNC(ir, loadcsjmp);
            return -1;

        case 0x30: /*PUSH*/
            if ((fetchdat & 0xc0) == 0xc0)
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
            sp_reg = LOAD_SP_WITH_OFFSET(ir, -4);
            uop_MEM_STORE_REG(ir, IREG_SS_base, sp_reg, src_reg);
            SUB_SP(ir, 4);
            return op_pc + 1;

        default:
            break;
    }
    return 0;
}

uint32_t
ropNOP(UNUSED(codeblock_t *block), UNUSED(ir_data_t *ir), UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    return op_pc;
}

uint32_t
ropCBW(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVSX(ir, IREG_AX, IREG_AL);

    return op_pc;
}
uint32_t
ropCDQ(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_SAR_IMM(ir, IREG_EDX, IREG_EAX, 31);

    return op_pc;
}
uint32_t
ropCWD(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_SAR_IMM(ir, IREG_DX, IREG_AX, 15);

    return op_pc;
}
uint32_t
ropCWDE(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVSX(ir, IREG_EAX, IREG_AX);

    return op_pc;
}

#define ropLxS(name, seg)                                                                      \
    uint32_t rop##name##_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),         \
                            uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                 \
    {                                                                                          \
        x86seg *target_seg = NULL;                                                             \
        int     dest_reg   = (fetchdat >> 3) & 7;                                              \
                                                                                               \
        if ((fetchdat & 0xc0) == 0xc0)                                                         \
            return 0;                                                                          \
                                                                                               \
        codegen_mark_code_present(block, cs + op_pc, 1);                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);            \
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, ireg_seg_base(target_seg), IREG_eaaddr, 2);  \
        uop_LOAD_SEG(ir, seg, IREG_temp1_W);                                                   \
        uop_MOV(ir, IREG_16(dest_reg), IREG_temp0_W);                                          \
                                                                                               \
        if (seg == &cpu_state.seg_ss)                                                          \
            CPU_BLOCK_END();                                                                   \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t rop##name##_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),         \
                            uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                 \
    {                                                                                          \
        x86seg *target_seg = NULL;                                                             \
        int     dest_reg   = (fetchdat >> 3) & 7;                                              \
                                                                                               \
        if ((fetchdat & 0xc0) == 0xc0)                                                         \
            return 0;                                                                          \
                                                                                               \
        codegen_mark_code_present(block, cs + op_pc, 1);                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);              \
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, ireg_seg_base(target_seg), IREG_eaaddr, 4);  \
        uop_LOAD_SEG(ir, seg, IREG_temp1_W);                                                   \
        uop_MOV(ir, IREG_32(dest_reg), IREG_temp0);                                            \
                                                                                               \
        if (seg == &cpu_state.seg_ss)                                                          \
            CPU_BLOCK_END();                                                                   \
                                                                                               \
        return op_pc + 1;                                                                      \
    }

// clang-format off
ropLxS(LDS, &cpu_state.seg_ds)
ropLxS(LES, &cpu_state.seg_es)
ropLxS(LFS, &cpu_state.seg_fs)
ropLxS(LGS, &cpu_state.seg_gs)
ropLxS(LSS, &cpu_state.seg_ss)
// clang-format on

uint32_t
ropCLC(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_CALL_FUNC(ir, flags_rebuild);
    uop_AND_IMM(ir, IREG_flags, IREG_flags, ~C_FLAG);
    return op_pc;
}
uint32_t
ropCMC(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_CALL_FUNC(ir, flags_rebuild);
    uop_XOR_IMM(ir, IREG_flags, IREG_flags, C_FLAG);
    return op_pc;
}
uint32_t
ropSTC(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_CALL_FUNC(ir, flags_rebuild);
    uop_OR_IMM(ir, IREG_flags, IREG_flags, C_FLAG);
    return op_pc;
}

uint32_t
ropCLD(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_AND_IMM(ir, IREG_flags, IREG_flags, ~D_FLAG);
    return op_pc;
}
uint32_t
ropSTD(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_OR_IMM(ir, IREG_flags, IREG_flags, D_FLAG);
    return op_pc;
}

uint32_t
ropCLI(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    if (!IOPLp && (cr4 & (CR4_VME | CR4_PVI)))
        return 0;

    uop_AND_IMM(ir, IREG_flags, IREG_flags, ~I_FLAG);
    return op_pc;
}
uint32_t
ropSTI(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    if (!IOPLp && (cr4 & (CR4_VME | CR4_PVI)))
        return 0;

    uop_OR_IMM(ir, IREG_flags, IREG_flags, I_FLAG);
    return op_pc;
}

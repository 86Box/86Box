#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86_ops.h"
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
#include "codegen_ops_helpers.h"
#include "codegen_ops_jit_wrappers.h"
#include "codegen_ops_misc.h"

extern int x87_legacy_pending(void);

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

/*MUL and IMUL with one operand (F6/F7 /4 /5). As the interpreter does it:
  the flags are rebuilt, CF and OF say whether the high half is more than the
  low half extended, and the other flags keep what they were. src is the
  operand, already a register or loaded into temp0.*/
static void
mul_one_operand(ir_data_t *ir, int size, int is_signed, int src)
{
    if (!codegen_flags_rebuild_known(ir, 0))
        uop_CALL_FUNC(ir, jit_flags_rebuild);

    if (size == 32) {
        if (is_signed)
            uop_IMUL_HI(ir, IREG_temp1, IREG_EAX, src);
        else
            uop_MUL_HI(ir, IREG_temp1, IREG_EAX, src);
        uop_IMUL(ir, IREG_EAX, IREG_EAX, src);
        uop_MOV(ir, IREG_EDX, IREG_temp1);
        if (is_signed) {
            uop_SAR_IMM(ir, IREG_temp3, IREG_EAX, 31);
            uop_CMP_SET(ir, IREG_temp2, IREG_temp1, IREG_temp3, UOP_CC_NZ);
        } else
            uop_TEST_SET(ir, IREG_temp2, IREG_temp1, UOP_CC_NZ);
    } else {
        /*The whole product fits 32 bits*/
        int lo = (size == 16) ? IREG_AX : IREG_AL;

        if (is_signed) {
            uop_MOVSX(ir, IREG_temp1, lo);
            uop_MOVSX(ir, IREG_temp2, src);
        } else {
            uop_MOVZX(ir, IREG_temp1, lo);
            uop_MOVZX(ir, IREG_temp2, src);
        }
        uop_IMUL(ir, IREG_temp1, IREG_temp1, IREG_temp2);
        if (size == 16) {
            uop_MOV(ir, IREG_AX, IREG_temp1_W);
            uop_SHR_IMM(ir, IREG_temp2, IREG_temp1, 16);
            uop_MOV(ir, IREG_DX, IREG_temp2_W);
        } else
            uop_MOV(ir, IREG_AX, IREG_temp1_W);
        if (is_signed) {
            if (size == 16)
                uop_MOVSX(ir, IREG_temp3, IREG_temp1_W);
            else
                uop_MOVSX(ir, IREG_temp3, IREG_temp1_B);
            uop_CMP_SET(ir, IREG_temp2, IREG_temp1, IREG_temp3, UOP_CC_NZ);
        } else {
            uop_SHR_IMM(ir, IREG_temp2, IREG_temp1, size);
            uop_TEST_SET(ir, IREG_temp2, IREG_temp2, UOP_CC_NZ);
        }
    }

    /*CF and OF, bits 0 and 11, from the overflow bit in temp2*/
    uop_SHL_IMM(ir, IREG_temp3, IREG_temp2, 11);
    uop_OR(ir, IREG_temp3, IREG_temp3, IREG_temp2);
    uop_AND_IMM(ir, IREG_flags, IREG_flags, (uint16_t) ~(C_FLAG | V_FLAG));
    uop_OR(ir, IREG_flags, IREG_flags, IREG_temp3_W);
}

uint32_t
ropF6(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;
    uint8_t imm_data;
    int     reg;

    if ((fetchdat & 0x30) == 0x30) /*DIV/IDIV*/
        return 0;
#ifndef CODEGEN_BACKEND_HAS_INT_UOPS
    if ((fetchdat & 0x30) == 0x20) /*MUL/IMUL*/
        return 0;
#endif

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
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        reg = IREG_temp0_B;
    }

    switch (fetchdat & 0x38) {
        case 0x00:
        case 0x08: /*TEST*/
            if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp1, cs + op_pc + 1);
                uop_AND(ir, IREG_flags_res_B, reg, IREG_temp1_B);
            } else {
                imm_data = fastreadb(cs + op_pc + 1);
                uop_AND_IMM(ir, IREG_flags_res_B, reg, imm_data);
                codegen_mark_code_present(block, cs + op_pc + 1, 1);
            }
            uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);

            codegen_flags_changed = 1;
            return op_pc + 2;

        case 0x10: /*NOT*/
            uop_XOR_IMM(ir, reg, reg, 0xff);
            if ((fetchdat & 0xc0) != 0xc0)
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, reg);

            /*NOT changes no flags: what the block knew of them still holds*/
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

        case 0x20: /*MUL*/
        case 0x28: /*IMUL*/
            mul_one_operand(ir, 8, (fetchdat & 0x08) ? 1 : 0, reg);
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

    if ((fetchdat & 0x30) == 0x30) /*DIV/IDIV*/
        return 0;
#ifndef CODEGEN_BACKEND_HAS_INT_UOPS
    if ((fetchdat & 0x30) == 0x20) /*MUL/IMUL*/
        return 0;
#endif

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
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        reg = IREG_temp0_W;
    }

    switch (fetchdat & 0x38) {
        case 0x00:
        case 0x08: /*TEST*/
            if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp1, cs + op_pc + 1);
                uop_AND(ir, IREG_flags_res_W, reg, IREG_temp1_W);
            } else {
                imm_data = fastreadw(cs + op_pc + 1);
                uop_AND_IMM(ir, IREG_flags_res_W, reg, imm_data);
                codegen_mark_code_present(block, cs + op_pc + 1, 2);
            }
            uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);

            codegen_flags_changed = 1;
            return op_pc + 3;

        case 0x10: /*NOT*/
            uop_XOR_IMM(ir, reg, reg, 0xffff);
            if ((fetchdat & 0xc0) != 0xc0)
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, reg);

            /*NOT changes no flags: what the block knew of them still holds*/
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

        case 0x20: /*MUL*/
        case 0x28: /*IMUL*/
            mul_one_operand(ir, 16, (fetchdat & 0x08) ? 1 : 0, reg);
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

    if ((fetchdat & 0x30) == 0x30) /*DIV/IDIV*/
        return 0;
#ifndef CODEGEN_BACKEND_HAS_INT_UOPS
    if ((fetchdat & 0x30) == 0x20) /*MUL/IMUL*/
        return 0;
#endif

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
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        reg = IREG_temp0;
    }

    switch (fetchdat & 0x38) {
        case 0x00:
        case 0x08: /*TEST*/
            if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp1, cs + op_pc + 1);
                uop_AND(ir, IREG_flags_res, reg, IREG_temp1);
            } else {
                imm_data = fastreadl(cs + op_pc + 1);
                uop_AND_IMM(ir, IREG_flags_res, reg, imm_data);
                codegen_mark_code_present(block, cs + op_pc + 1, 4);
            }
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);

            codegen_flags_changed = 1;
            return op_pc + 5;

        case 0x10: /*NOT*/
            uop_XOR_IMM(ir, reg, reg, 0xffffffff);
            if ((fetchdat & 0xc0) != 0xc0)
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, reg);

            /*NOT changes no flags: what the block knew of them still holds*/
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

        case 0x20: /*MUL*/
        case 0x28: /*IMUL*/
            mul_one_operand(ir, 32, (fetchdat & 0x08) ? 1 : 0, reg);
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

    if (needs_rebuild && !codegen_flags_rebuild_known(ir, 1)) {
#if defined __aarch64__ || defined _M_ARM64 || defined __amd64__ || defined _M_X64
        /*flags_rebuild_c() writes only flags, so the cached registers are
          written back but left loaded, and flags is reloaded after it.*/
        uop_CALL_FUNC_ORDERED(ir, jit_flags_rebuild_c);
        uop_MOVZX_REG_PTR_16(ir, IREG_flags, &cpu_state.flags);
#else
        uop_CALL_FUNC(ir, jit_flags_rebuild_c);
#endif
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
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, ((fetchdat & 0x38) == 0x28) ? 3 : 1); /*JMP far: the whole pointer*/
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
            if ((fetchdat & 0xc0) == 0xc0) {
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                /*The target before the push: CALL SP/ESP jumps to its old value*/
                uop_MOV(ir, IREG_temp2_W, src_reg);
                src_reg = IREG_temp2_W;
            }
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
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, ((fetchdat & 0x38) == 0x28) ? 5 : 3); /*JMP far: the whole pointer*/
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
            if ((fetchdat & 0xc0) == 0xc0) {
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
                /*The target before the push: CALL SP/ESP jumps to its old value*/
                uop_MOV(ir, IREG_temp2, src_reg);
                src_reg = IREG_temp2;
            }
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

#define ropLxS(name, seg, end_block)                                                           \
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
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3); /*the whole pointer*/         \
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), (op_32 & 0x200) ? IREG_eaaddr : IREG_eaa16); \
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, ireg_seg_base(target_seg), (op_32 & 0x200) ? IREG_eaaddr : IREG_eaa16, 2); \
        uop_LOAD_SEG(ir, seg, IREG_temp1_W);                                                   \
        uop_MOV(ir, IREG_16(dest_reg), IREG_temp0_W);                                          \
                                                                                               \
        if (end_block)                                                                         \
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
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 5); /*the whole pointer*/         \
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), (op_32 & 0x200) ? IREG_eaaddr : IREG_eaa16);              \
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, ireg_seg_base(target_seg), (op_32 & 0x200) ? IREG_eaaddr : IREG_eaa16, 4);  \
        uop_LOAD_SEG(ir, seg, IREG_temp1_W);                                                   \
        uop_MOV(ir, IREG_32(dest_reg), IREG_temp0);                                            \
                                                                                               \
        if (end_block)                                                                         \
            CPU_BLOCK_END();                                                                   \
                                                                                               \
        return op_pc + 1;                                                                      \
    }

// clang-format off
ropLxS(LDS, &cpu_state.seg_ds, 0)
ropLxS(LES, &cpu_state.seg_es, 0)
ropLxS(LFS, &cpu_state.seg_fs, 0)
ropLxS(LGS, &cpu_state.seg_gs, 0)
ropLxS(LSS, &cpu_state.seg_ss, 1)
// clang-format on

uint32_t
ropCLC(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    call_flags_rebuild(ir, jit_flags_rebuild);
    uop_AND_IMM(ir, IREG_flags, IREG_flags, ~C_FLAG);
    return op_pc;
}
uint32_t
ropCMC(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    call_flags_rebuild(ir, jit_flags_rebuild);
    uop_XOR_IMM(ir, IREG_flags, IREG_flags, C_FLAG);
    return op_pc;
}
uint32_t
ropSTC(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    call_flags_rebuild(ir, jit_flags_rebuild);
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
ropPREFETCH(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    if ((fetchdat & 0xc0) == 0xc0)
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    return op_pc + 1;
}

/*EMMS: the same checks and state change as FEMMS, in the same helper*/
uint32_t
ropEMMS(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_CALL_FUNC_RESULT(ir, IREG_temp0, codegen_femms);
    uop_CMP_IMM_JZ(ir, IREG_temp0, 1, codegen_exit_rout);

    codegen_mmx_entered = 0;
    codegen_fpu_entered = 0;
    return op_pc;
}

uint32_t
ropFEMMS(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_CALL_FUNC_RESULT(ir, IREG_temp0, codegen_femms);
    uop_CMP_IMM_JZ(ir, IREG_temp0, 1, codegen_exit_rout);

    codegen_mmx_entered = 0;
    codegen_fpu_entered = 0;
    return op_pc;
}

/* Blocks are keyed on protected mode but not on CPL or IOPL, so outside
   real mode the privilege check has to run with the block. CPL > IOPL takes
   the interpreter's CLI/STI, which raises #GP(0) or updates VIF under
   VME/PVI; Windows relies on that fault to virtualize IF. */
static uint32_t
ropCLI_STI(ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int set)
{
    if (cpu_cur_status & CPU_STATUS_PMODE) {
        int jump_uop;

        codegen_accumulate_flush(ir);
        uop_MOVZX_REG_PTR_8(ir, IREG_temp0, &cpu_state.seg_cs.access);
        uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 5);
        uop_AND_IMM(ir, IREG_temp0, IREG_temp0, 3);
        uop_MOVZX(ir, IREG_temp1, IREG_flags);
        uop_SHR_IMM(ir, IREG_temp1, IREG_temp1, 12);
        uop_AND_IMM(ir, IREG_temp1, IREG_temp1, 3);
        jump_uop = uop_CMP_JBE_DEST(ir, IREG_temp0, IREG_temp1);

        uop_MOV_IMM(ir, IREG_pc, op_pc);
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        uop_CALL_INSTRUCTION_FUNC(ir, x86_dynarec_opcodes[(opcode | op_32) & 0x3ff], fetchdat);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
    }

    if (set)
        uop_OR_IMM(ir, IREG_flags, IREG_flags, I_FLAG);
    else
        uop_AND_IMM(ir, IREG_flags, IREG_flags, ~I_FLAG);

    return op_pc;
}

uint32_t
ropCLI(UNUSED(codeblock_t *block), ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return ropCLI_STI(ir, opcode, fetchdat, op_32, op_pc, 0);
}
uint32_t
ropSTI(UNUSED(codeblock_t *block), ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return ropCLI_STI(ir, opcode, fetchdat, op_32, op_pc, 1);
}

uint32_t
ropBSWAP(UNUSED(codeblock_t *block), ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
#ifndef CODEGEN_BACKEND_HAS_INT_UOPS
    return 0;
#endif
    /*The whole register with an operand size prefix as well, as the
      interpreter does it*/
    uop_BSWAP(ir, IREG_32(opcode & 7), IREG_32(opcode & 7));
    return op_pc;
}

uint32_t
ropLAHF(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    if (!codegen_flags_rebuild_known(ir, 0))
        uop_CALL_FUNC(ir, jit_flags_rebuild);
    uop_MOVZX(ir, IREG_temp0, IREG_flags);
    uop_MOV(ir, IREG_AH, IREG_temp0_B);
    return op_pc;
}
uint32_t
ropSAHF(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    if (!codegen_flags_rebuild_known(ir, 0))
        uop_CALL_FUNC(ir, jit_flags_rebuild);
    uop_MOVZX(ir, IREG_temp0, IREG_AH);
    uop_AND_IMM(ir, IREG_temp0, IREG_temp0, 0xd5);
    uop_OR_IMM(ir, IREG_temp0, IREG_temp0, 0x02);
    uop_AND_IMM(ir, IREG_flags, IREG_flags, 0xff00);
    uop_OR(ir, IREG_flags, IREG_flags, IREG_temp0_W);
    return op_pc;
}

/*BSF and BSR. As the interpreter does it: the flags are rebuilt, ZF says
  whether the source was zero, the destination keeps its value if it was,
  and the other flags keep what they were.*/
static uint32_t
bs_common(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int size32, int reverse)
{
    int dest_reg = (fetchdat >> 3) & 7;
    int src;

#ifndef CODEGEN_BACKEND_HAS_INT_UOPS
    return 0;
#endif
    codegen_mark_code_present(block, cs + op_pc, 1);
    if (!codegen_flags_rebuild_known(ir, 0))
        uop_CALL_FUNC(ir, jit_flags_rebuild);

    if ((fetchdat & 0xc0) == 0xc0) {
        if (size32)
            src = IREG_32(fetchdat & 7);
        else {
            uop_MOVZX(ir, IREG_temp0, IREG_16(fetchdat & 7));
            src = IREG_temp0;
        }
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, size32 ? 3 : 1);
        if (size32)
            uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        else {
            uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
            uop_MOVZX(ir, IREG_temp0, IREG_temp0_W);
        }
        src = IREG_temp0;
    }

    /*ZF first: the source may be the destination*/
    uop_TEST_SET(ir, IREG_temp3, src, UOP_CC_Z);
    if (size32) {
        if (reverse)
            uop_BSR(ir, IREG_32(dest_reg), src, IREG_32(dest_reg));
        else
            uop_BSF(ir, IREG_32(dest_reg), src, IREG_32(dest_reg));
    } else {
        uop_MOVZX(ir, IREG_temp1, IREG_16(dest_reg));
        if (reverse)
            uop_BSR(ir, IREG_temp2, src, IREG_temp1);
        else
            uop_BSF(ir, IREG_temp2, src, IREG_temp1);
        uop_MOV(ir, IREG_16(dest_reg), IREG_temp2_W);
    }
    uop_SHL_IMM(ir, IREG_temp3, IREG_temp3, 6);
    uop_AND_IMM(ir, IREG_flags, IREG_flags, (uint16_t) ~Z_FLAG);
    uop_OR(ir, IREG_flags, IREG_flags, IREG_temp3_W);

    return op_pc + 1;
}

uint32_t
ropBSF_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return bs_common(block, ir, fetchdat, op_32, op_pc, 0, 0);
}
uint32_t
ropBSF_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return bs_common(block, ir, fetchdat, op_32, op_pc, 1, 0);
}
uint32_t
ropBSR_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return bs_common(block, ir, fetchdat, op_32, op_pc, 0, 1);
}
uint32_t
ropBSR_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return bs_common(block, ir, fetchdat, op_32, op_pc, 1, 1);
}

/*BT, BTS, BTR and BTC. As in the interpreter, the flags are rebuilt and
  only CF changes. A bit offset in a register is signed and, with a memory
  operand, moves the address by whole operands; an immediate one, or any
  offset with a register operand, is taken modulo the operand size.*/
enum {
    BIT_TEST,
    BIT_SET,
    BIT_RESET,
    BIT_COMPLEMENT
};

static uint32_t
bit_common(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int kind, int is32, int imm_form)
{
    x86seg *target_seg = NULL;
    int     bit_reg    = (fetchdat >> 3) & 7;
    int     mask       = is32 ? 31 : 15;
    int     is_reg     = (fetchdat & 0xc0) == 0xc0;
    uint8_t imm        = 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if (!codegen_flags_rebuild_known(ir, 0))
        uop_CALL_FUNC(ir, jit_flags_rebuild);
    if (!is_reg) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    }
    if (imm_form) {
        imm = fastreadb(cs + op_pc + 1) & mask;
        codegen_mark_code_present(block, cs + op_pc + 1, 1);
    }

    if (is_reg) {
        if (is32)
            uop_MOV(ir, IREG_temp0, IREG_32(fetchdat & 7));
        else
            uop_MOVZX(ir, IREG_temp0, IREG_16(fetchdat & 7));
    } else {
        if (!imm_form) {
            if (is32) {
                uop_SAR_IMM(ir, IREG_temp1, IREG_32(bit_reg), 5);
                uop_SHL_IMM(ir, IREG_temp1, IREG_temp1, 2);
            } else {
                uop_MOVSX(ir, IREG_temp1, IREG_16(bit_reg));
                uop_SAR_IMM(ir, IREG_temp1, IREG_temp1, 4);
                uop_SHL_IMM(ir, IREG_temp1, IREG_temp1, 1);
            }
            uop_ADD(ir, IREG_eaaddr, IREG_eaaddr, IREG_temp1);
            if (!(op_32 & 0x200)) /*16-bit addressing wraps*/
                uop_AND_IMM(ir, IREG_eaaddr, IREG_eaaddr, 0xffff);
        }
        if (kind == BIT_TEST)
            codegen_check_seg_read(block, ir, target_seg);
        else
            codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, is32 ? 3 : 1);
        if (is32)
            uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        else {
            uop_MEM_LOAD_REG(ir, IREG_temp3_W, ireg_seg_base(target_seg), IREG_eaaddr);
            uop_MOVZX(ir, IREG_temp0, IREG_temp3_W);
        }
    }

    /*CF = the bit*/
    if (imm_form) {
        if (imm)
            uop_SHR_IMM(ir, IREG_temp2, IREG_temp0, imm);
        else
            uop_MOV(ir, IREG_temp2, IREG_temp0);
    } else {
        uop_AND_IMM(ir, IREG_temp1, IREG_32(bit_reg), mask);
        uop_SHR(ir, IREG_temp2, IREG_temp0, IREG_temp1);
    }
    uop_AND_IMM(ir, IREG_temp2, IREG_temp2, 1);
    uop_AND_IMM(ir, IREG_flags, IREG_flags, ~C_FLAG);
    uop_OR(ir, IREG_flags, IREG_flags, IREG_temp2_W);

    if (kind != BIT_TEST) {
        if (imm_form) {
            if (kind == BIT_SET)
                uop_OR_IMM(ir, IREG_temp0, IREG_temp0, 1u << imm);
            else if (kind == BIT_RESET)
                uop_AND_IMM(ir, IREG_temp0, IREG_temp0, ~(1u << imm));
            else
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, 1u << imm);
        } else {
            uop_MOV_IMM(ir, IREG_temp2, 1);
            uop_SHL(ir, IREG_temp2, IREG_temp2, IREG_temp1);
            if (kind == BIT_SET)
                uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp2);
            else if (kind == BIT_RESET) {
                uop_XOR_IMM(ir, IREG_temp2, IREG_temp2, 0xffffffff);
                uop_AND(ir, IREG_temp0, IREG_temp0, IREG_temp2);
            } else
                uop_XOR(ir, IREG_temp0, IREG_temp0, IREG_temp2);
        }

        if (is_reg) {
            if (is32)
                uop_MOV(ir, IREG_32(fetchdat & 7), IREG_temp0);
            else
                uop_MOV(ir, IREG_16(fetchdat & 7), IREG_temp0_W);
        } else {
            if (is32)
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
            else
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
        }
    }

    return op_pc + (imm_form ? 2 : 1);
}

#define ROP_BIT(name, kind)                                                                                                         \
    uint32_t rop##name##_w_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc) \
    {                                                                                                                               \
        return bit_common(block, ir, fetchdat, op_32, op_pc, kind, 0, 0);                                                           \
    }                                                                                                                               \
    uint32_t rop##name##_l_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc) \
    {                                                                                                                               \
        return bit_common(block, ir, fetchdat, op_32, op_pc, kind, 1, 0);                                                           \
    }

// clang-format off
ROP_BIT(BT,  BIT_TEST)
ROP_BIT(BTS, BIT_SET)
ROP_BIT(BTR, BIT_RESET)
ROP_BIT(BTC, BIT_COMPLEMENT)
// clang-format on

/*0F BA: /4 BT, /5 BTS, /6 BTR, /7 BTC with an immediate; /0-/3 are #UD*/
uint32_t
ropBA_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    if (!(fetchdat & 0x20))
        return 0;
    return bit_common(block, ir, fetchdat, op_32, op_pc, (fetchdat >> 3) & 3, 0, 1);
}
uint32_t
ropBA_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    if (!(fetchdat & 0x20))
        return 0;
    return bit_common(block, ir, fetchdat, op_32, op_pc, (fetchdat >> 3) & 3, 1, 1);
}

/*XADD and CMPXCHG, for byte, word and dword operands. Every value is taken
  zero-extended into a 32-bit temporary, so the register and memory forms,
  and an r/m register that is also the other operand, all go the same way.*/
static int
ireg_sized(int reg, int size)
{
    return (size == 8) ? IREG_8(reg) : ((size == 16) ? IREG_16(reg) : IREG_32(reg));
}
static int
temp_sized(int n, int size)
{
    static const int temps[4][3] = {
        {IREG_temp0_B, IREG_temp0_W, IREG_temp0},
        { IREG_temp1_B, IREG_temp1_W, IREG_temp1},
        { IREG_temp2_B, IREG_temp2_W, IREG_temp2},
        { IREG_temp3_B, IREG_temp3_W, IREG_temp3}
    };
    return temps[n][(size == 8) ? 0 : ((size == 16) ? 1 : 2)];
}
static const int temp_l[4] = { IREG_temp0, IREG_temp1, IREG_temp2, IREG_temp3 };

/*temp n = the register, zero-extended*/
static void
load_zx(ir_data_t *ir, int n, int reg, int size)
{
    if (size == 32)
        uop_MOV(ir, temp_l[n], IREG_32(reg));
    else
        uop_MOVZX(ir, temp_l[n], ireg_sized(reg, size));
}
static void
set_flags_res(ir_data_t *ir, int n, int size)
{
    if (size == 32)
        uop_MOV(ir, IREG_flags_res, temp_l[n]);
    else
        uop_MOVZX(ir, IREG_flags_res, temp_sized(n, size));
}

/*The destination: r/m into temp0 (zero-extended); target_seg is set for a
  memory operand, NULL for a register*/
static x86seg *
xchg_dest_load(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t *op_pc, int size)
{
    x86seg *target_seg = NULL;

    if ((fetchdat & 0xc0) == 0xc0)
        load_zx(ir, 0, fetchdat & 7, size);
    else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size / 8) - 1);
        if (size == 32)
            uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        else {
            uop_MEM_LOAD_REG(ir, temp_sized(3, size), ireg_seg_base(target_seg), IREG_eaaddr);
            uop_MOVZX(ir, IREG_temp0, temp_sized(3, size));
        }
    }
    return target_seg;
}
/*r/m = temp n*/
static void
xchg_dest_store(ir_data_t *ir, uint32_t fetchdat, x86seg *target_seg, int n, int size)
{
    if (!target_seg)
        uop_MOV(ir, ireg_sized(fetchdat & 7, size), temp_sized(n, size));
    else
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, temp_sized(n, size));
}

static uint32_t
xadd_common(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int size)
{
    int     src_reg = (fetchdat >> 3) & 7;
    x86seg *target_seg;

    codegen_mark_code_present(block, cs + op_pc, 1);
    target_seg = xchg_dest_load(block, ir, fetchdat, op_32, &op_pc, size);
    load_zx(ir, 1, src_reg, size);
    uop_ADD(ir, IREG_temp2, IREG_temp1, IREG_temp0);

    /*SRC <- DEST, then DEST <- TEMP: with one register as both, the sum
      stays. The memory form stores first, so a fault leaves the register*/
    if (!target_seg) {
        uop_MOV(ir, ireg_sized(src_reg, size), temp_sized(0, size));
        xchg_dest_store(ir, fetchdat, target_seg, 2, size);
    } else {
        xchg_dest_store(ir, fetchdat, target_seg, 2, size);
        uop_MOV(ir, ireg_sized(src_reg, size), temp_sized(0, size));
    }

    uop_MOV(ir, IREG_flags_op1, IREG_temp1);
    uop_MOV(ir, IREG_flags_op2, IREG_temp0);
    set_flags_res(ir, 2, size);
    uop_MOV_IMM(ir, IREG_flags_op, (size == 8) ? FLAGS_ADD8 : ((size == 16) ? FLAGS_ADD16 : FLAGS_ADD32));
    codegen_flags_changed = 1;

    return op_pc + 1;
}

/*Branchless: the destination is written either way (the source on a match,
  its own value back otherwise), and the accumulator ends up holding the old
  destination in both cases.*/
static uint32_t
cmpxchg_common(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int size)
{
    int     src_reg = (fetchdat >> 3) & 7;
    x86seg *target_seg;

#ifndef CODEGEN_BACKEND_HAS_INT_UOPS
    return 0;
#endif
    codegen_mark_code_present(block, cs + op_pc, 1);
    target_seg = xchg_dest_load(block, ir, fetchdat, op_32, &op_pc, size);
    load_zx(ir, 1, 0, size);       /*the accumulator*/
    load_zx(ir, 2, src_reg, size); /*the source*/

    /*temp3 = all ones on a match, else 0; temp2 = the value to write*/
    uop_CMP_SET(ir, IREG_temp3, IREG_temp1, IREG_temp0, UOP_CC_Z);
    uop_SHL_IMM(ir, IREG_temp3, IREG_temp3, 31);
    uop_SAR_IMM(ir, IREG_temp3, IREG_temp3, 31);
    uop_XOR(ir, IREG_temp2, IREG_temp2, IREG_temp0);
    uop_AND(ir, IREG_temp2, IREG_temp2, IREG_temp3);
    uop_XOR(ir, IREG_temp2, IREG_temp2, IREG_temp0);

    if (!target_seg) {
        /*The accumulator first: with r/m the accumulator itself, the source wins*/
        uop_MOV(ir, ireg_sized(0, size), temp_sized(0, size));
        xchg_dest_store(ir, fetchdat, target_seg, 2, size);
    } else {
        /*The store first: if it faults the accumulator keeps its value*/
        xchg_dest_store(ir, fetchdat, target_seg, 2, size);
        uop_MOV(ir, ireg_sized(0, size), temp_sized(0, size));
    }

    /*Flags as CMP accumulator, destination*/
    uop_MOV(ir, IREG_flags_op1, IREG_temp1);
    uop_MOV(ir, IREG_flags_op2, IREG_temp0);
    uop_SUB(ir, IREG_temp3, IREG_temp1, IREG_temp0);
    set_flags_res(ir, 3, size);
    uop_MOV_IMM(ir, IREG_flags_op, (size == 8) ? FLAGS_SUB8 : ((size == 16) ? FLAGS_SUB16 : FLAGS_SUB32));
    codegen_flags_changed = 1;

    return op_pc + 1;
}

uint32_t
ropXADD_b(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return xadd_common(block, ir, fetchdat, op_32, op_pc, 8);
}
uint32_t
ropXADD_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return xadd_common(block, ir, fetchdat, op_32, op_pc, 16);
}
uint32_t
ropXADD_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return xadd_common(block, ir, fetchdat, op_32, op_pc, 32);
}
uint32_t
ropCMPXCHG_b(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return cmpxchg_common(block, ir, fetchdat, op_32, op_pc, 8);
}
uint32_t
ropCMPXCHG_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return cmpxchg_common(block, ir, fetchdat, op_32, op_pc, 16);
}
uint32_t
ropCMPXCHG_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return cmpxchg_common(block, ir, fetchdat, op_32, op_pc, 32);
}

/*WAIT/FWAIT: #NM with CR0.MP and CR0.TS both set, else nothing, as the
  interpreter's opWAIT without softfloat (which also checks pending x87
  exceptions, and is left to the interpreter)*/
uint32_t
ropWAIT(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int jump_uop;

    if (fpu_softfloat)
        return 0;

    /*As on the interpreter path, the cycles so far are charged before WAIT
      can fault*/
    codegen_accumulate_flush(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_REG_PTR(ir, IREG_temp0, &cr0);
    uop_AND_IMM(ir, IREG_temp0, IREG_temp0, 0xa);
    jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0xa);
    uop_LOAD_FUNC_ARG_IMM(ir, 0, 7);
    uop_CALL_FUNC(ir, x86_int);
    uop_JMP(ir, codegen_exit_rout);
    uop_set_jump_dest(ir, jump_uop);
    /* WAIT is where a pending unmasked exception is reported, as the
       interpreter's WAIT does it: #MF, or FERR# and the wait for IRQ 13.
       With ES clear the helper only clears fpu_ignne, so it is called when
       either is set */
    uop_MOVZX(ir, IREG_temp0, IREG_NPXS);
    uop_AND_IMM(ir, IREG_temp0, IREG_temp0, FPU_SW_Summary);
    uop_MOV_REG_PTR(ir, IREG_temp1, &fpu_ignne);
    uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp1);
    jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
    uop_CALL_FUNC_RESULT(ir, IREG_temp3, x87_legacy_pending);
    uop_CMP_IMM_JZ(ir, IREG_temp3, 1, codegen_exit_rout);
    uop_set_jump_dest(ir, jump_uop);

    return op_pc;
}

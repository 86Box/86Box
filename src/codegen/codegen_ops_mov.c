#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "x86_flags.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_mov.h"

uint32_t
ropMOV_rb_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0, cs + op_pc);
        uop_MOV(ir, IREG_8(opcode & 7), IREG_temp0_B);
    } else {
        uop_MOV_IMM(ir, IREG_8(opcode & 7), fastreadb(cs + op_pc));
        codegen_mark_code_present(block, cs + op_pc, 1);
    }
    return op_pc + 1;
}
uint32_t
ropMOV_rw_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp0, cs + op_pc);
        uop_MOV(ir, IREG_16(opcode & 7), IREG_temp0_W);
    } else {
        uop_MOV_IMM(ir, IREG_16(opcode & 7), fastreadw(cs + op_pc));
        codegen_mark_code_present(block, cs + op_pc, 2);
    }
    return op_pc + 2;
}
uint32_t
ropMOV_rl_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_32(opcode & 7), cs + op_pc);
    } else {
        fetchdat = fastreadl(cs + op_pc);
        uop_MOV_IMM(ir, IREG_32(opcode & 7), fetchdat);
        codegen_mark_code_present(block, cs + op_pc, 4);
    }
    return op_pc + 4;
}

uint32_t
ropMOV_b_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;
        uop_MOV(ir, IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_8(src_reg));
    }

    return op_pc + 1;
}
uint32_t
ropMOV_w_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;
        uop_MOV(ir, IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_16(src_reg));
    }

    return op_pc + 1;
}
uint32_t
ropMOV_l_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;
        uop_MOV(ir, IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_32(src_reg));
    }

    return op_pc + 1;
}
uint32_t
ropMOV_r_b(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOV(ir, IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_8(dest_reg), ireg_seg_base(target_seg), IREG_eaaddr);
    }

    return op_pc + 1;
}
uint32_t
ropMOV_r_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOV(ir, IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_16(dest_reg), ireg_seg_base(target_seg), IREG_eaaddr);
    }

    return op_pc + 1;
}
uint32_t
ropMOV_r_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOV(ir, IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_32(dest_reg), ireg_seg_base(target_seg), IREG_eaaddr);
    }

    return op_pc + 1;
}

/*The address of A0-A3: in a block rewritten again and again
  (CODEBLOCK_NO_IMMEDIATES) read from RAM into eaaddr as the block runs,
  and not marked; else compiled in. Returns 1 when eaaddr holds it.*/
static int
mov_abs_addr(codeblock_t *block, ir_data_t *ir, uint32_t op_32, uint32_t op_pc, uint32_t *addr)
{
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        if (op_32 & 0x200)
            LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_eaaddr, cs + op_pc);
        else
            LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_eaaddr, cs + op_pc);
        return 1;
    }
    *addr = (op_32 & 0x200) ? fastreadl(cs + op_pc) : fastreadw(cs + op_pc);
    codegen_mark_code_present(block, cs + op_pc, (op_32 & 0x200) ? 4 : 2);
    return 0;
}

uint32_t
ropMOV_AL_abs(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t addr = 0;
    int      dyn  = mov_abs_addr(block, ir, op_32, op_pc, &addr);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_read(block, ir, op_ea_seg);
    if (!dyn)
        uop_MOV_IMM(ir, IREG_eaaddr, addr);
    CHECK_SEG_LIMITS(block, ir, op_ea_seg, IREG_eaaddr, 0);
    if (dyn)
        uop_MEM_LOAD_REG(ir, IREG_AL, ireg_seg_base(op_ea_seg), IREG_eaaddr);
    else
        uop_MEM_LOAD_ABS(ir, IREG_AL, ireg_seg_base(op_ea_seg), addr);

    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}
uint32_t
ropMOV_AX_abs(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t addr = 0;
    int      dyn  = mov_abs_addr(block, ir, op_32, op_pc, &addr);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_read(block, ir, op_ea_seg);
    if (!dyn)
        uop_MOV_IMM(ir, IREG_eaaddr, addr);
    CHECK_SEG_LIMITS(block, ir, op_ea_seg, IREG_eaaddr, 1);
    if (dyn)
        uop_MEM_LOAD_REG(ir, IREG_AX, ireg_seg_base(op_ea_seg), IREG_eaaddr);
    else
        uop_MEM_LOAD_ABS(ir, IREG_AX, ireg_seg_base(op_ea_seg), addr);

    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}
uint32_t
ropMOV_EAX_abs(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t addr = 0;
    int      dyn  = mov_abs_addr(block, ir, op_32, op_pc, &addr);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_read(block, ir, op_ea_seg);
    if (!dyn)
        uop_MOV_IMM(ir, IREG_eaaddr, addr);
    CHECK_SEG_LIMITS(block, ir, op_ea_seg, IREG_eaaddr, 3);
    if (dyn)
        uop_MEM_LOAD_REG(ir, IREG_EAX, ireg_seg_base(op_ea_seg), IREG_eaaddr);
    else
        uop_MEM_LOAD_ABS(ir, IREG_EAX, ireg_seg_base(op_ea_seg), addr);

    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}

uint32_t
ropMOV_abs_AL(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t addr = 0;
    int      dyn  = mov_abs_addr(block, ir, op_32, op_pc, &addr);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_write(block, ir, op_ea_seg);
    if (!dyn)
        uop_MOV_IMM(ir, IREG_eaaddr, addr);
    CHECK_SEG_LIMITS(block, ir, op_ea_seg, IREG_eaaddr, 0);
    if (dyn)
        uop_MEM_STORE_REG(ir, ireg_seg_base(op_ea_seg), IREG_eaaddr, IREG_AL);
    else
        uop_MEM_STORE_ABS(ir, ireg_seg_base(op_ea_seg), addr, IREG_AL);

    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}
uint32_t
ropMOV_abs_AX(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t addr = 0;
    int      dyn  = mov_abs_addr(block, ir, op_32, op_pc, &addr);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_write(block, ir, op_ea_seg);
    if (!dyn)
        uop_MOV_IMM(ir, IREG_eaaddr, addr);
    CHECK_SEG_LIMITS(block, ir, op_ea_seg, IREG_eaaddr, 1);
    if (dyn)
        uop_MEM_STORE_REG(ir, ireg_seg_base(op_ea_seg), IREG_eaaddr, IREG_AX);
    else
        uop_MEM_STORE_ABS(ir, ireg_seg_base(op_ea_seg), addr, IREG_AX);

    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}
uint32_t
ropMOV_abs_EAX(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t addr = 0;
    int      dyn  = mov_abs_addr(block, ir, op_32, op_pc, &addr);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_write(block, ir, op_ea_seg);
    if (!dyn)
        uop_MOV_IMM(ir, IREG_eaaddr, addr);
    CHECK_SEG_LIMITS(block, ir, op_ea_seg, IREG_eaaddr, 3);
    if (dyn)
        uop_MEM_STORE_REG(ir, ireg_seg_base(op_ea_seg), IREG_eaaddr, IREG_EAX);
    else
        uop_MEM_STORE_ABS(ir, ireg_seg_base(op_ea_seg), addr, IREG_EAX);

    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}

uint32_t
ropMOV_b_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;
    uint8_t imm;

    if (fetchdat & 0x38) /*C6/C7 /1-/7 are #UD*/
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0, cs + op_pc + 1);
            uop_MOV(ir, IREG_8(dest_reg), IREG_temp0_B);
        } else {
            imm = fastreadb(cs + op_pc + 1);
            uop_MOV_IMM(ir, IREG_8(dest_reg), imm);
        }
    } else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0, cs + op_pc + 1);
            uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
        } else {
            imm = fastreadb(cs + op_pc + 1);
            uop_MEM_STORE_IMM_8(ir, ireg_seg_base(target_seg), IREG_eaaddr, imm);
        }
    }

    if (!(block->flags & CODEBLOCK_NO_IMMEDIATES))
        codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}
uint32_t
ropMOV_w_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg  *target_seg;
    uint16_t imm;

    if (fetchdat & 0x38) /*C6/C7 /1-/7 are #UD*/
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp0, cs + op_pc + 1);
            uop_MOV(ir, IREG_16(dest_reg), IREG_temp0_W);
        } else {
            imm = fastreadw(cs + op_pc + 1);
            uop_MOV_IMM(ir, IREG_16(dest_reg), imm);
        }
    } else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp0, cs + op_pc + 1);
            uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
        } else {
            imm = fastreadw(cs + op_pc + 1);
            uop_MEM_STORE_IMM_16(ir, ireg_seg_base(target_seg), IREG_eaaddr, imm);
        }
    }

    if (!(block->flags & CODEBLOCK_NO_IMMEDIATES))
        codegen_mark_code_present(block, cs + op_pc + 1, 2);
    return op_pc + 3;
}
uint32_t
ropMOV_l_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg  *target_seg;
    uint32_t imm;

    if (fetchdat & 0x38) /*C6/C7 /1-/7 are #UD*/
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_32(dest_reg), cs + op_pc + 1);
        } else {
            imm = fastreadl(cs + op_pc + 1);
            uop_MOV_IMM(ir, IREG_32(dest_reg), imm);
        }
    } else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp0, cs + op_pc + 1);
            uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
        } else {
            imm = fastreadl(cs + op_pc + 1);
            uop_MEM_STORE_IMM_32(ir, ireg_seg_base(target_seg), IREG_eaaddr, imm);
        }
    }

    if (!(block->flags & CODEBLOCK_NO_IMMEDIATES))
        codegen_mark_code_present(block, cs + op_pc + 1, 4);
    return op_pc + 5;
}

uint32_t
ropMOV_w_seg(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg;

    codegen_mark_code_present(block, cs + op_pc, 1);
    switch (fetchdat & 0x38) {
        case 0x00: /*ES*/
            src_reg = IREG_ES_seg_W;
            break;
        case 0x08: /*CS*/
            src_reg = IREG_CS_seg_W;
            break;
        case 0x18: /*DS*/
            src_reg = IREG_DS_seg_W;
            break;
        case 0x10: /*SS*/
            src_reg = IREG_SS_seg_W;
            break;
        case 0x20: /*FS*/
            src_reg = IREG_FS_seg_W;
            break;
        case 0x28: /*GS*/
            src_reg = IREG_GS_seg_W;
            break;
        default:
            return 0;
    }

    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;
        uop_MOV(ir, IREG_16(dest_reg), src_reg);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, src_reg);
    }

    return op_pc + 1;
}
uint32_t
ropMOV_l_seg(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg;

    codegen_mark_code_present(block, cs + op_pc, 1);
    switch (fetchdat & 0x38) {
        case 0x00: /*ES*/
            src_reg = IREG_ES_seg_W;
            break;
        case 0x08: /*CS*/
            src_reg = IREG_CS_seg_W;
            break;
        case 0x18: /*DS*/
            src_reg = IREG_DS_seg_W;
            break;
        case 0x10: /*SS*/
            src_reg = IREG_SS_seg_W;
            break;
        case 0x20: /*FS*/
            src_reg = IREG_FS_seg_W;
            break;
        case 0x28: /*GS*/
            src_reg = IREG_GS_seg_W;
            break;
        default:
            return 0;
    }

    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;
        uop_MOVZX(ir, IREG_32(dest_reg), src_reg);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, src_reg);
    }

    return op_pc + 1;
}

/*The bytes of a ModR/M operand: the ModR/M byte, a SIB byte and the
  displacement*/
static uint32_t
modrm_length(uint32_t fetchdat, uint32_t op_32)
{
    uint8_t  modrm = fetchdat & 0xff;
    uint8_t  sib   = (fetchdat >> 8) & 0xff;
    uint32_t len   = 1;

    if ((modrm & 0xc0) == 0xc0)
        return len;
    if (op_32 & 0x200) {
        if ((modrm & 7) == 4) {
            len++;
            if (((modrm & 0xc0) == 0x00) && ((sib & 7) == 5))
                len += 4;
        } else if ((modrm & 0xc7) == 0x05)
            len += 4;
        if ((modrm & 0xc0) == 0x40)
            len++;
        else if ((modrm & 0xc0) == 0x80)
            len += 4;
    } else {
        if ((modrm & 0xc7) == 0x06)
            len += 2;
        else if ((modrm & 0xc0) == 0x40)
            len++;
        else if ((modrm & 0xc0) == 0x80)
            len += 2;
    }
    return len;
}

/*1 when the instruction at addr cannot be compiled as the second of a MOV SS
  pair. In every mode: one whose bytes are not all on the page the MOV SS
  starts on, as reading them here could fault before the MOV SS has run; and
  an x87 escape, WAIT, or any 0F instruction, whose compiled forms can leave
  the block before the instruction runs (the FPU and MMX entry, class and
  pending exception checks), which would let an interrupt in between the two.
  In protected mode also one compiled with the stack size the block was
  compiled for, which the new SS can change: a push or a pop, a near or far
  call or return, ENTER, LEAVE, INT, IRET, or another SS load. Prefixes are
  stepped over; an instruction that is all prefixes counts too*/
static int
mov_ss_next_denied(uint32_t page, uint32_t addr, int pmode)
{
    uint8_t opcode = 0;
    uint8_t modrm;
    int     n;

    for (n = 0; n < 15; n++) {
        if (((addr + n) & ~0xfff) != page)
            return 1;
        opcode = fastreadb(addr + n);
        if ((opcode != 0x26) && (opcode != 0x2e) && (opcode != 0x36) && (opcode != 0x3e) && (opcode != 0x64) && (opcode != 0x65) &&
            (opcode != 0x66) && (opcode != 0x67) && (opcode != 0xf0) && (opcode != 0xf2) && (opcode != 0xf3))
            break;
    }
    if (n == 15)
        return 1;
    if (((opcode & 0xf8) == 0xd8) || (opcode == 0x9b) || (opcode == 0x0f))
        return 1;
    if (!pmode)
        return 0;
    if ((opcode & 0xf0) == 0x50)
        return 1;
    switch (opcode) {
        case 0x06: /*PUSH and POP of a segment register*/
        case 0x07:
        case 0x0e:
        case 0x16:
        case 0x17:
        case 0x1e:
        case 0x1f:
        case 0x60: /*PUSHA, POPA*/
        case 0x61:
        case 0x68: /*PUSH imm*/
        case 0x6a:
        case 0x8f: /*POP r/m*/
        case 0x9a: /*CALL far*/
        case 0x9c: /*PUSHF, POPF*/
        case 0x9d:
        case 0xc2: /*RET, RETF*/
        case 0xc3:
        case 0xca:
        case 0xcb:
        case 0xc8: /*ENTER, LEAVE*/
        case 0xc9:
        case 0xcc: /*INT, IRET*/
        case 0xcd:
        case 0xce:
        case 0xcf:
        case 0xe8: /*CALL*/
            return 1;
        case 0x8e: /*MOV SS*/
        case 0xff: /*CALL, CALL far, PUSH*/
            if (((addr + n + 1) & ~0xfff) != page)
                return 1;
            modrm = fastreadb(addr + n + 1) & 0x38;
            if (opcode == 0x8e)
                return modrm == 0x10;
            return (modrm == 0x10) || (modrm == 0x18) || (modrm == 0x30);
        default:
            return 0;
    }
}

/*MOV SS, as the interpreter's opMOV_seg_w: the load, then the next
  instruction with no interrupt, NMI or trap check between the two, which
  the op does by running that instruction itself. In the compile pass the op
  first compiles that instruction into the block (codegen_mov_ss_pair_valid,
  codegen_mov_ss_pair_pc), and in a compiled block nothing is checked between
  instructions. In protected mode the new SS can change the stack size, which
  the block is keyed on: the block ends after the pair. The pair is left to
  the op when the next instruction cannot be its second
  (mov_ss_next_denied), and when the block has no room for both or the
  register versions no room for this one: the loop would end the block
  between the two.*/
static uint32_t
mov_ss(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    const int pmode = (msw & 1) && !(cpu_state.eflags & VM_FLAG);

    if (!codegen_instruction_room())
        return 0;
    /*The versions this instruction writes (at most 5 of temp0, 6 of eaaddr,
      3 of temp3, 1 of the cycles and of oldpc) stay at or below
      REG_VERSION_MAX, where the next instruction expects them*/
    if ((reg_last_version[IREG_GET_REG(IREG_temp0)] + 6 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_eaaddr)] + 8 > REG_VERSION_MAX) ||
        (reg_last_version[IREG_GET_REG(IREG_temp3)] + 4 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_cycles)] + 2 > REG_VERSION_MAX) ||
        (reg_last_version[IREG_GET_REG(IREG_oldpc)] + 2 > REG_VERSION_MAX))
        return 0;
    if (mov_ss_next_denied((cs + cpu_state.oldpc) & ~0xfff, cs + op_pc + modrm_length(fetchdat, op_32), pmode))
        return 0;

    /*The cycles so far are charged before the load can fault, as they are
      before an interpreter call*/
    codegen_accumulate_flush(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    if ((fetchdat & 0xc0) == 0xc0)
        uop_MOV(ir, IREG_temp0_W, IREG_16(fetchdat & 7));
    else {
        x86seg *target_seg;

        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if (target_seg != &cpu_state.seg_cs) /*the op tests for the null selector only: an execute-only CS is read*/
            codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
    }
    uop_LOAD_SEG(ir, &cpu_state.seg_ss, IREG_temp0_W);

    codegen_mov_ss_pair_valid = 1;
    codegen_mov_ss_pair_pc    = cs + cpu_state.oldpc;
    if (pmode)
        CPU_BLOCK_END();

    return op_pc + 1;
}

uint32_t
ropMOV_seg_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int     src_reg;
    x86seg *rseg;

    codegen_mark_code_present(block, cs + op_pc, 1);
    switch (fetchdat & 0x38) {
        case 0x00: /*ES*/
            rseg = &cpu_state.seg_es;
            break;
        case 0x18: /*DS*/
            rseg = &cpu_state.seg_ds;
            break;
        case 0x10: /*SS*/
            return mov_ss(block, ir, fetchdat, op_32, op_pc);
        case 0x20: /*FS*/
            rseg = &cpu_state.seg_fs;
            break;
        case 0x28: /*GS*/
            rseg = &cpu_state.seg_gs;
            break;
        default:
            return 0;
    }

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if ((fetchdat & 0xc0) == 0xc0) {
        uop_MOV(ir, IREG_temp0_W, IREG_16(fetchdat & 7));
        src_reg = IREG_temp0_W;
    } else {
        x86seg *target_seg;

        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        src_reg = IREG_temp0_W;
    }

    uop_LOAD_SEG(ir, rseg, src_reg);

    return op_pc + 1;
}

uint32_t
ropMOVSX_16_8(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOVSX(ir, IREG_16(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVSX(ir, IREG_16(dest_reg), IREG_temp0_B);
    }

    return op_pc + 1;
}
uint32_t
ropMOVSX_32_8(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOVSX(ir, IREG_32(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVSX(ir, IREG_32(dest_reg), IREG_temp0_B);
    }

    return op_pc + 1;
}
uint32_t
ropMOVSX_32_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOVSX(ir, IREG_32(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVSX(ir, IREG_32(dest_reg), IREG_temp0_W);
    }

    return op_pc + 1;
}

uint32_t
ropMOVZX_16_8(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOVZX(ir, IREG_16(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_16(dest_reg), IREG_temp0_B);
    }

    return op_pc + 1;
}
uint32_t
ropMOVZX_32_8(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOVZX(ir, IREG_32(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_32(dest_reg), IREG_temp0_B);
    }

    return op_pc + 1;
}
uint32_t
ropMOVZX_32_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOVZX(ir, IREG_32(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_32(dest_reg), IREG_temp0_W);
    }

    return op_pc + 1;
}

uint32_t
ropXCHG_AX(UNUSED(codeblock_t *block), ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int reg2 = IREG_16(opcode & 7);

    uop_MOV(ir, IREG_temp0_W, IREG_AX);
    uop_MOV(ir, IREG_AX, reg2);
    uop_MOV(ir, reg2, IREG_temp0_W);

    return op_pc;
}
uint32_t
ropXCHG_EAX(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int reg2 = IREG_32(opcode & 7);

    uop_MOV(ir, IREG_temp0, IREG_EAX);
    uop_MOV(ir, IREG_EAX, reg2);
    uop_MOV(ir, reg2, IREG_temp0);

    return op_pc;
}

uint32_t
ropXCHG_8(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int reg1 = IREG_8((fetchdat >> 3) & 7);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int reg2 = IREG_8(fetchdat & 7);

        uop_MOV(ir, IREG_temp0_B, reg1);
        uop_MOV(ir, reg1, reg2);
        uop_MOV(ir, reg2, IREG_temp0_B);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);

        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, reg1);
        uop_MOV(ir, reg1, IREG_temp0_B);
    }

    return op_pc + 1;
}
uint32_t
ropXCHG_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int reg1 = IREG_16((fetchdat >> 3) & 7);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int reg2 = IREG_16(fetchdat & 7);

        uop_MOV(ir, IREG_temp0_W, reg1);
        uop_MOV(ir, reg1, reg2);
        uop_MOV(ir, reg2, IREG_temp0_W);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);

        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, reg1);
        uop_MOV(ir, reg1, IREG_temp0_W);
    }

    return op_pc + 1;
}
uint32_t
ropXCHG_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int reg1 = IREG_32((fetchdat >> 3) & 7);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int reg2 = IREG_32(fetchdat & 7);

        uop_MOV(ir, IREG_temp0, reg1);
        uop_MOV(ir, reg1, reg2);
        uop_MOV(ir, reg2, IREG_temp0);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);

        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, reg1);
        uop_MOV(ir, reg1, IREG_temp0);
    }

    return op_pc + 1;
}

uint32_t
ropXLAT(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_read(block, ir, op_ea_seg);

    uop_MOVZX(ir, IREG_eaaddr, IREG_AL);
    uop_ADD(ir, IREG_eaaddr, IREG_eaaddr, IREG_EBX);
    if (!(op_32 & 0x200))
        uop_AND_IMM(ir, IREG_eaaddr, IREG_eaaddr, 0xffff);
    /*The limit check, on the address just made*/
    CHECK_SEG_LIMITS(block, ir, op_ea_seg, IREG_eaaddr, 0);

    uop_MEM_LOAD_REG(ir, IREG_AL, ireg_seg_base(op_ea_seg), IREG_eaaddr);

    return op_pc;
}

/*MOVS, STOS and LODS without a REP prefix (REP forms stay in the
  interpreter). As its 386 path: every access is checked before the index
  registers move, so a fault leaves SI/DI as they were. The step is +size
  or -size by DF, worked out at run time.*/
enum {
    STR_MOVS,
    STR_STOS,
    STR_LODS
};

static uint32_t
string_common(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t op_32, uint32_t op_pc, int kind)
{
    int size = (opcode & 1) ? ((op_32 & 0x100) ? 4 : 2) : 1;
    int a32  = op_32 & 0x200;
    int acc  = (size == 1) ? IREG_AL : ((size == 2) ? IREG_AX : IREG_EAX);
    int t0   = (size == 1) ? IREG_temp0_B : ((size == 2) ? IREG_temp0_W : IREG_temp0);
    int shift = (size == 1) ? 1 : ((size == 2) ? 2 : 3);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    /*The source, op_ea_seg:[E]SI*/
    if (kind != STR_STOS) {
        if (a32)
            uop_MOV(ir, IREG_eaaddr, IREG_ESI);
        else
            uop_MOVZX(ir, IREG_eaaddr, IREG_SI);
        codegen_check_seg_read(block, ir, op_ea_seg);
        CHECK_SEG_LIMITS(block, ir, op_ea_seg, IREG_eaaddr, size - 1);
    }
    /*The destination, ES:[E]DI, in temp1*/
    if (kind != STR_LODS) {
        if (a32)
            uop_MOV(ir, IREG_temp1, IREG_EDI);
        else
            uop_MOVZX(ir, IREG_temp1, IREG_DI);
        codegen_check_seg_write(block, ir, &cpu_state.seg_es);
        CHECK_SEG_LIMITS(block, ir, &cpu_state.seg_es, IREG_temp1, size - 1);
    }

    if (kind == STR_LODS)
        uop_MEM_LOAD_REG(ir, acc, ireg_seg_base(op_ea_seg), IREG_eaaddr);
    else if (kind == STR_STOS)
        uop_MEM_STORE_REG(ir, IREG_ES_base, IREG_temp1, acc);
    else {
        uop_MEM_LOAD_REG(ir, t0, ireg_seg_base(op_ea_seg), IREG_eaaddr);
        uop_MEM_STORE_REG(ir, IREG_ES_base, IREG_temp1, t0);
    }

    /*temp2 = size - (DF ? 2 * size : 0)*/
    uop_MOVZX(ir, IREG_temp2, IREG_flags);
    uop_SHR_IMM(ir, IREG_temp2, IREG_temp2, 10);
    uop_AND_IMM(ir, IREG_temp2, IREG_temp2, 1);
    uop_SHL_IMM(ir, IREG_temp2, IREG_temp2, shift);
    uop_MOV_IMM(ir, IREG_temp3, size);
    uop_SUB(ir, IREG_temp2, IREG_temp3, IREG_temp2);
    if (kind != STR_STOS) {
        if (a32)
            uop_ADD(ir, IREG_ESI, IREG_ESI, IREG_temp2);
        else
            uop_ADD(ir, IREG_SI, IREG_SI, IREG_temp2_W);
    }
    if (kind != STR_LODS) {
        if (a32)
            uop_ADD(ir, IREG_EDI, IREG_EDI, IREG_temp2);
        else
            uop_ADD(ir, IREG_DI, IREG_DI, IREG_temp2_W);
    }

    return op_pc;
}

uint32_t
ropMOVS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return string_common(block, ir, opcode, op_32, op_pc, STR_MOVS);
}
uint32_t
ropSTOS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return string_common(block, ir, opcode, op_32, op_pc, STR_STOS);
}
uint32_t
ropLODS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return string_common(block, ir, opcode, op_32, op_pc, STR_LODS);
}

/*SCAS and CMPS without REP: SUB flags of accumulator - ES:[DI], or of
  [SI] - ES:[DI], as the interpreter sets them. ES:[DI] is checked and read
  first, as there; the index registers move only after both reads.*/
static uint32_t
string_compare(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t op_32, uint32_t op_pc, int is_cmps)
{
    int size  = (opcode & 1) ? ((op_32 & 0x100) ? 4 : 2) : 1;
    int a32   = op_32 & 0x200;
    int t3    = (size == 1) ? IREG_temp3_B : ((size == 2) ? IREG_temp3_W : IREG_temp3);
    int shift = (size == 1) ? 1 : ((size == 2) ? 2 : 3);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    /*ES:[E]DI, into temp2 (zero-extended); its address in temp1*/
    if (a32)
        uop_MOV(ir, IREG_temp1, IREG_EDI);
    else
        uop_MOVZX(ir, IREG_temp1, IREG_DI);
    codegen_check_seg_read(block, ir, &cpu_state.seg_es);
    CHECK_SEG_LIMITS(block, ir, &cpu_state.seg_es, IREG_temp1, size - 1);
    if (size == 4)
        uop_MEM_LOAD_REG(ir, IREG_temp2, IREG_ES_base, IREG_temp1);
    else {
        uop_MEM_LOAD_REG(ir, t3, IREG_ES_base, IREG_temp1);
        uop_MOVZX(ir, IREG_temp2, t3);
    }

    /*The other operand into temp0: op_ea_seg:[E]SI, or the accumulator*/
    if (is_cmps) {
        if (a32)
            uop_MOV(ir, IREG_eaaddr, IREG_ESI);
        else
            uop_MOVZX(ir, IREG_eaaddr, IREG_SI);
        codegen_check_seg_read(block, ir, op_ea_seg);
        CHECK_SEG_LIMITS(block, ir, op_ea_seg, IREG_eaaddr, size - 1);
        if (size == 4)
            uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(op_ea_seg), IREG_eaaddr);
        else {
            uop_MEM_LOAD_REG(ir, t3, ireg_seg_base(op_ea_seg), IREG_eaaddr);
            uop_MOVZX(ir, IREG_temp0, t3);
        }
    } else {
        if (size == 4)
            uop_MOV(ir, IREG_temp0, IREG_EAX);
        else
            uop_MOVZX(ir, IREG_temp0, (size == 1) ? IREG_AL : IREG_AX);
    }

    uop_MOV(ir, IREG_flags_op1, IREG_temp0);
    uop_MOV(ir, IREG_flags_op2, IREG_temp2);
    uop_SUB(ir, IREG_temp3, IREG_temp0, IREG_temp2);
    if (size == 4)
        uop_MOV(ir, IREG_flags_res, IREG_temp3);
    else
        uop_MOVZX(ir, IREG_flags_res, t3);
    uop_MOV_IMM(ir, IREG_flags_op, (size == 1) ? FLAGS_SUB8 : ((size == 2) ? FLAGS_SUB16 : FLAGS_SUB32));
    codegen_flags_changed = 1;

    /*temp2 = size - (DF ? 2 * size : 0), then step*/
    uop_MOVZX(ir, IREG_temp2, IREG_flags);
    uop_SHR_IMM(ir, IREG_temp2, IREG_temp2, 10);
    uop_AND_IMM(ir, IREG_temp2, IREG_temp2, 1);
    uop_SHL_IMM(ir, IREG_temp2, IREG_temp2, shift);
    uop_MOV_IMM(ir, IREG_temp3, size);
    uop_SUB(ir, IREG_temp3, IREG_temp3, IREG_temp2);
    if (is_cmps) {
        if (a32)
            uop_ADD(ir, IREG_ESI, IREG_ESI, IREG_temp3);
        else
            uop_ADD(ir, IREG_SI, IREG_SI, IREG_temp3_W);
    }
    if (a32)
        uop_ADD(ir, IREG_EDI, IREG_EDI, IREG_temp3);
    else
        uop_ADD(ir, IREG_DI, IREG_DI, IREG_temp3_W);

    return op_pc;
}

uint32_t
ropCMPS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return string_compare(block, ir, opcode, op_32, op_pc, 1);
}
uint32_t
ropSCAS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return string_compare(block, ir, opcode, op_32, op_pc, 0);
}

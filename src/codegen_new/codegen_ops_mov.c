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

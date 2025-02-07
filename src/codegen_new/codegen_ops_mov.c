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
ropMOV_rb_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint8_t imm = fastreadb(cs + op_pc);

    uop_MOV_IMM(ir, IREG_8(opcode & 7), imm);

    codegen_mark_code_present(block, cs + op_pc, 1);
    return op_pc + 1;
}
uint32_t
ropMOV_rw_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t imm = fastreadw(cs + op_pc);

    uop_MOV_IMM(ir, IREG_16(opcode & 7), imm);

    codegen_mark_code_present(block, cs + op_pc, 2);
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
        uop_MEM_LOAD_REG(ir, IREG_32(dest_reg), ireg_seg_base(target_seg), IREG_eaaddr);
    }

    return op_pc + 1;
}

uint32_t
ropMOV_AL_abs(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t addr;

    if (op_32 & 0x200)
        addr = fastreadl(cs + op_pc);
    else
        addr = fastreadw(cs + op_pc);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_read(block, ir, op_ea_seg);
    uop_MEM_LOAD_ABS(ir, IREG_AL, ireg_seg_base(op_ea_seg), addr);

    codegen_mark_code_present(block, cs + op_pc, (op_32 & 0x200) ? 4 : 2);
    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}
uint32_t
ropMOV_AX_abs(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t addr;

    if (op_32 & 0x200)
        addr = fastreadl(cs + op_pc);
    else
        addr = fastreadw(cs + op_pc);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_read(block, ir, op_ea_seg);
    uop_MEM_LOAD_ABS(ir, IREG_AX, ireg_seg_base(op_ea_seg), addr);

    codegen_mark_code_present(block, cs + op_pc, (op_32 & 0x200) ? 4 : 2);
    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}
uint32_t
ropMOV_EAX_abs(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t addr = 0;

    if (op_32 & 0x200) {
        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_eaaddr, cs + op_pc);
        } else {
            addr = fastreadl(cs + op_pc);
            codegen_mark_code_present(block, cs + op_pc, 4);
        }
    } else {
        addr = fastreadw(cs + op_pc);
        codegen_mark_code_present(block, cs + op_pc, 2);
    }

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_read(block, ir, op_ea_seg);
    if ((block->flags & CODEBLOCK_NO_IMMEDIATES) && (op_32 & 0x200))
        uop_MEM_LOAD_REG(ir, IREG_EAX, ireg_seg_base(op_ea_seg), IREG_eaaddr);
    else
        uop_MEM_LOAD_ABS(ir, IREG_EAX, ireg_seg_base(op_ea_seg), addr);

    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}

uint32_t
ropMOV_abs_AL(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t addr;

    if (op_32 & 0x200)
        addr = fastreadl(cs + op_pc);
    else
        addr = fastreadw(cs + op_pc);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_write(block, ir, op_ea_seg);
    uop_MEM_STORE_ABS(ir, ireg_seg_base(op_ea_seg), addr, IREG_AL);

    codegen_mark_code_present(block, cs + op_pc, (op_32 & 0x200) ? 4 : 2);
    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}
uint32_t
ropMOV_abs_AX(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t addr;

    if (op_32 & 0x200)
        addr = fastreadl(cs + op_pc);
    else
        addr = fastreadw(cs + op_pc);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_write(block, ir, op_ea_seg);
    uop_MEM_STORE_ABS(ir, ireg_seg_base(op_ea_seg), addr, IREG_AX);

    codegen_mark_code_present(block, cs + op_pc, (op_32 & 0x200) ? 4 : 2);
    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}
uint32_t
ropMOV_abs_EAX(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t addr;

    if (op_32 & 0x200)
        addr = fastreadl(cs + op_pc);
    else
        addr = fastreadw(cs + op_pc);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_write(block, ir, op_ea_seg);
    uop_MEM_STORE_ABS(ir, ireg_seg_base(op_ea_seg), addr, IREG_EAX);

    codegen_mark_code_present(block, cs + op_pc, (op_32 & 0x200) ? 4 : 2);
    return op_pc + ((op_32 & 0x200) ? 4 : 2);
}

uint32_t
ropMOV_b_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;
    uint8_t imm;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        imm = fastreadb(cs + op_pc + 1);
        uop_MOV_IMM(ir, IREG_8(dest_reg), imm);
    } else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        imm = fastreadb(cs + op_pc + 1);
        uop_MEM_STORE_IMM_8(ir, ireg_seg_base(target_seg), IREG_eaaddr, imm);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}
uint32_t
ropMOV_w_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg  *target_seg;
    uint16_t imm;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        imm = fastreadw(cs + op_pc + 1);
        uop_MOV_IMM(ir, IREG_16(dest_reg), imm);
    } else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        imm = fastreadw(cs + op_pc + 1);
        uop_MEM_STORE_IMM_16(ir, ireg_seg_base(target_seg), IREG_eaaddr, imm);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 2);
    return op_pc + 3;
}
uint32_t
ropMOV_l_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg  *target_seg;
    uint32_t imm;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        imm = fastreadl(cs + op_pc + 1);
        uop_MOV_IMM(ir, IREG_32(dest_reg), imm);
    } else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        imm = fastreadl(cs + op_pc + 1);
        uop_MEM_STORE_IMM_32(ir, ireg_seg_base(target_seg), IREG_eaaddr, imm);
    }

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

        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, reg1);
        uop_MOV(ir, reg1, IREG_temp0);
    }

    return op_pc + 1;
}

uint32_t
ropXLAT(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    uop_MOVZX(ir, IREG_eaaddr, IREG_AL);
    uop_ADD(ir, IREG_eaaddr, IREG_eaaddr, IREG_EBX);
    if (!(op_32 & 0x200))
        uop_AND_IMM(ir, IREG_eaaddr, IREG_eaaddr, 0xffff);

    uop_MEM_LOAD_REG(ir, IREG_AL, ireg_seg_base(op_ea_seg), IREG_eaaddr);

    return op_pc;
}

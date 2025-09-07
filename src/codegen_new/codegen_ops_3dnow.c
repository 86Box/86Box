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
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_3dnow.h"
#include "codegen_ops_helpers.h"

#define ropParith(func)                                                                            \
    uint32_t rop##func(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),                  \
                       uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                          \
    {                                                                                              \
        int dest_reg = (fetchdat >> 3) & 7;                                                        \
                                                                                                   \
        uop_MMX_ENTER(ir);                                                                         \
        codegen_mark_code_present(block, cs + op_pc, 1);                                           \
        if ((fetchdat & 0xc0) == 0xc0) {                                                           \
            int src_reg = fetchdat & 7;                                                            \
            uop_##func(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_MM(src_reg));                \
        } else {                                                                                   \
            x86seg *target_seg;                                                                    \
                                                                                                   \
            uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
            target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
            codegen_check_seg_read(block, ir, target_seg);                                         \
            uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);            \
            uop_##func(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_temp0_Q);                    \
        }                                                                                          \
                                                                                                   \
        codegen_mark_code_present(block, cs + op_pc + 1, 1);                                       \
        return op_pc + 2;                                                                          \
    }

// clang-format off
ropParith(PFADD)
ropParith(PFCMPEQ)
ropParith(PFCMPGE)
ropParith(PFCMPGT)
ropParith(PFMAX)
ropParith(PFMIN)
ropParith(PFMUL)
ropParith(PFSUB)
    // clang-format on

uint32_t ropPF2ID(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PF2ID(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PF2ID(ir, IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFSUBR(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PFSUB(ir, IREG_MM(dest_reg), IREG_MM(src_reg), IREG_MM(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PFSUB(ir, IREG_MM(dest_reg), IREG_temp0_Q, IREG_MM(dest_reg));
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPI2FD(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PI2FD(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PI2FD(ir, IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFRCPIT(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOV(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_MM(dest_reg), ireg_seg_base(target_seg), IREG_eaaddr);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}
uint32_t
ropPFRCP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PFRCP(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PFRCP(ir, IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}
uint32_t
ropPFRSQRT(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PFRSQRT(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PFRSQRT(ir, IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFRSQIT1(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MMX_ENTER(ir);

    codegen_mark_code_present(block, cs + op_pc, 2);
    return op_pc + 2;
}

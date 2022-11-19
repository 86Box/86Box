#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "x86.h"
#include "x86_flags.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_mmx_shift.h"
#include "codegen_ops_helpers.h"

uint32_t
ropPSxxW_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int reg   = fetchdat & 7;
    int op    = fetchdat & 0x38;
    int shift = fastreadb(cs + op_pc + 1);

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    switch (op) {
        case 0x10: /*PSRLW*/
            uop_PSRLW_IMM(ir, IREG_MM(reg), IREG_MM(reg), shift);
            break;
        case 0x20: /*PSRAW*/
            uop_PSRAW_IMM(ir, IREG_MM(reg), IREG_MM(reg), shift);
            break;
        case 0x30: /*PSLLW*/
            uop_PSLLW_IMM(ir, IREG_MM(reg), IREG_MM(reg), shift);
            break;
        default:
            return 0;
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}
uint32_t
ropPSxxD_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int reg   = fetchdat & 7;
    int op    = fetchdat & 0x38;
    int shift = fastreadb(cs + op_pc + 1);

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    switch (op) {
        case 0x10: /*PSRLD*/
            uop_PSRLD_IMM(ir, IREG_MM(reg), IREG_MM(reg), shift);
            break;
        case 0x20: /*PSRAD*/
            uop_PSRAD_IMM(ir, IREG_MM(reg), IREG_MM(reg), shift);
            break;
        case 0x30: /*PSLLD*/
            uop_PSLLD_IMM(ir, IREG_MM(reg), IREG_MM(reg), shift);
            break;
        default:
            return 0;
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}
uint32_t
ropPSxxQ_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int reg   = fetchdat & 7;
    int op    = fetchdat & 0x38;
    int shift = fastreadb(cs + op_pc + 1);

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    switch (op) {
        case 0x10: /*PSRLQ*/
            uop_PSRLQ_IMM(ir, IREG_MM(reg), IREG_MM(reg), shift);
            break;
        case 0x20: /*PSRAQ*/
            uop_PSRAQ_IMM(ir, IREG_MM(reg), IREG_MM(reg), shift);
            break;
        case 0x30: /*PSLLQ*/
            uop_PSLLQ_IMM(ir, IREG_MM(reg), IREG_MM(reg), shift);
            break;
        default:
            return 0;
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

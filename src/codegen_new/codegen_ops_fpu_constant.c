#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "x86.h"
#include "x86_flags.h"
#include "386_common.h"
#include "x87.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_fpu_constant.h"
#include "codegen_ops_helpers.h"

uint32_t ropFLD1(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_FP_ENTER(ir);
        uop_MOV_IMM(ir, IREG_temp0, 1);
        uop_MOV_DOUBLE_INT(ir, IREG_ST(-1), IREG_temp0);
        uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID);
        fpu_PUSH(block, ir);

        return op_pc;
}
uint32_t ropFLDZ(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_FP_ENTER(ir);
        uop_MOV_IMM(ir, IREG_temp0, 0);
        uop_MOV_DOUBLE_INT(ir, IREG_ST(-1), IREG_temp0);
        uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID);
        fpu_PUSH(block, ir);

        return op_pc;
}
